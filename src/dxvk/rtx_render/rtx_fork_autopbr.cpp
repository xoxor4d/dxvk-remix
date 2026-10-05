// src/dxvk/rtx_render/rtx_fork_autopbr.cpp
//
// Fork-owned file. AutoPBR: collects material -> game texture associations
// (fed by rtx_fork_game_textures.cpp), dumps the textures and writes the files
// the offline conversion scripts consume, all under the fixed folder
// <game>/rtx-remix/imgdump/:
//   dump/<wc|mc>/<folder>/{color,normal,specular}.dds   one folder per material
//   associations.json                                   GTAIV AutoPBR schema, version 1
//   comp_world_autopbr.usda / comp_mesh_autopbr.usda    normal / roughness overrides per mat_<hash>
//
// Thread model: all state sits behind one mutex. Associations arrive from the
// API thread (D3D9 draws) and the render thread (API materials); exports are
// issued from the render thread at the end of each frame (endFrame), throttled
// by rtx.autopbr.exportsPerFrame. Textures are written on the asset exporter
// thread, associations.json / usda on an owned worker (FileWriter).
//
// See docs/RemixAutoPbrAPI.md.

#include "rtx_fork_autopbr.h"

#include "rtx_asset_exporter.h"
#include "rtx_context.h"
#include "rtx_fork_game_state.h"
#include "rtx_fork_hooks.h"
#include "rtx_imgui.h"

#include "../dxvk_buffer.h"
#include "../dxvk_objects.h"

#include "../../util/log/log.h"
#include "../../util/util_env.h"
#include "../../util/util_filesys.h"
#include "../../util/util_math.h"
#include "../../util/util_once.h"

#include <gli/gli.hpp>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <thread>
#include <condition_variable>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace dxvk {
namespace {

  using game_textures::Usage;

  constexpr const char* kUsageFiles[Usage::Count] = { "color.dds", "normal.dds", "specular.dds" };
  constexpr const char* kCollectingKey = "__autopbr.collecting";
  constexpr const char* kAssociationsFile = "associations.json";
  constexpr const char* kDumpDir = "dump";
  constexpr const char* kWorldCategory = "wc";
  constexpr const char* kMeshCategory = "mc";
  constexpr const char* kWorldUsdaFile = "comp_world_autopbr.usda";
  constexpr const char* kMeshUsdaFile = "comp_mesh_autopbr.usda";

  struct Association {
    XXH64_hash_t material = kEmptyHash;
    XXH64_hash_t hashes[Usage::Count] = {};
    uint32_t widths[Usage::Count] = {};
    uint32_t heights[Usage::Count] = {};
    std::string names[Usage::Count];
    std::string shaderName;
    std::string materialName;
  };

  struct ExportJob {
    std::filesystem::path path;
    Rc<DxvkImageView> view;
  };

  struct LiveMaterial {
    XXH64_hash_t key = kEmptyHash;
    std::shared_ptr<const game_textures::TextureSet> textures;
  };

  struct State {
    std::mutex mutex;
    std::vector<Association> associations;
    std::unordered_map<XXH64_hash_t, size_t> index;
    // Dump file paths on disk or queued.
    std::unordered_set<std::string> known;
    std::deque<ExportJob> queue;
    std::unordered_map<remixapi_MaterialHandle, LiveMaterial> liveMaterials;
    uint32_t unsaved = 0;
    uint64_t exported = 0;
    // associations.json was merged (or deliberately cleared) this session.
    bool loaded = false;
    std::string status;
  };

  State& state() {
    static State s_state;
    return s_state;
  }

  std::atomic<bool> s_collecting { false };
  // Queue non-empty or autosave pending; lets endFrame skip the lock.
  std::atomic<bool> s_hasWork { false };
  // API material textures whose data is still missing (retried by game_textures).
  std::atomic<uint32_t> s_waitingForData { 0 };
  // Serializes associations.json / usda reads and writes.
  std::mutex s_fileMutex;

  // ---------------------------------------------------------------------------
  // Paths / formatting

  std::filesystem::path imgdumpDir() {
    const std::filesystem::path root = util::RtxFileSys::isInitialized()
      ? util::RtxFileSys::rootPath()
      : std::filesystem::path(env::getExePath()).parent_path();
    return root / "rtx-remix" / "imgdump";
  }

  std::string toHex(XXH64_hash_t hash) {
    char buffer[17];
    std::snprintf(buffer, sizeof(buffer), "%016llX", static_cast<unsigned long long>(hash));
    return buffer;
  }

  bool parseHex(const std::string& text, XXH64_hash_t& out) {
    if (text.empty() || text.size() > 16) {
      return false;
    }
    XXH64_hash_t value = 0;
    for (const char c : text) {
      value <<= 4;
      if (c >= '0' && c <= '9') value |= XXH64_hash_t(c - '0');
      else if (c >= 'a' && c <= 'f') value |= XXH64_hash_t(c - 'a' + 10);
      else if (c >= 'A' && c <= 'F') value |= XXH64_hash_t(c - 'A' + 10);
      else return false;
    }
    out = value;
    return true;
  }

  // Replaces characters illegal in Windows file names, path separators and the
  // usda asset delimiter '@'.
  std::string sanitizeFolderName(const std::string& name) {
    std::string out = name;
    for (char& c : out) {
      if (static_cast<unsigned char>(c) < 0x20 || std::strchr("<>:\"/\\|?*@", c) != nullptr) {
        c = '_';
      }
    }
    // Windows strips trailing dots and spaces.
    for (auto it = out.rbegin(); it != out.rend() && (*it == '.' || *it == ' '); ++it) {
      *it = '_';
    }

    std::string stem = out.substr(0, out.find('.'));
    std::transform(stem.begin(), stem.end(), stem.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    const bool reserved = stem == "CON" || stem == "PRN" || stem == "AUX" || stem == "NUL" ||
      (stem.size() == 4 && (stem.compare(0, 3, "COM") == 0 || stem.compare(0, 3, "LPT") == 0) && stem[3] >= '1' && stem[3] <= '9');
    if (out.empty() || reserved) {
      out += '_';
    }
    return out;
  }

  struct MaterialLocation {
    const char* category;
    std::string folder;
  };

  const char* categoryPrefix(const std::string& name) {
    for (const char* category : { kWorldCategory, kMeshCategory }) {
      if (name.size() > 3 && name.compare(0, 2, category) == 0 && name[2] == '/') {
        return category;
      }
    }
    return nullptr;
  }

  bool hasCategoryPrefix(const std::string& name) {
    return categoryPrefix(name) != nullptr;
  }

  // material_name is "wc/<name>" (world) or "mc/<name>" (mesh). Without a
  // prefix the material is filed as world under its colormap name or hash.
  MaterialLocation materialLocation(const Association& a) {
    if (const char* category = categoryPrefix(a.materialName)) {
      return { category, sanitizeFolderName(a.materialName.substr(3)) };
    }
    if (!a.names[Usage::Color].empty()) {
      return { kWorldCategory, sanitizeFolderName(a.names[Usage::Color]) };
    }
    const XXH64_hash_t hash = a.hashes[Usage::Color] != kEmptyHash ? a.hashes[Usage::Color] : a.material;
    return { kWorldCategory, toHex(hash) };
  }

  bool writeFileAtomic(const std::filesystem::path& path, const std::string& contents) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);

    std::filesystem::path tmpPath = path;
    tmpPath += ".tmp";
    {
      std::ofstream file(tmpPath, std::ios::binary | std::ios::trunc);
      file.write(contents.data(), static_cast<std::streamsize>(contents.size()));
      file.close();
      if (!file) {
        Logger::err(str::format("[AutoPBR] Failed to write ", tmpPath.string()));
        return false;
      }
    }

    std::filesystem::rename(tmpPath, path, ec);
    if (ec) {
      Logger::err(str::format("[AutoPBR] Failed to replace ", path.string(), ": ", ec.message()));
      std::filesystem::remove(tmpPath, ec);
      return false;
    }
    return true;
  }

  // ---------------------------------------------------------------------------
  // associations.json (fixed flat schema, version 1)

  void appendJsonString(std::string& out, const std::string& value) {
    out += '"';
    for (const char c : value) {
      switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          char escaped[7];
          std::snprintf(escaped, sizeof(escaped), "\\u%04X", static_cast<unsigned>(c));
          out += escaped;
        } else {
          out += c;
        }
      }
    }
    out += '"';
  }

  std::string serializeAssociations(const std::vector<Association>& associations) {
    std::string out;
    out.reserve(256 + associations.size() * 768);
    out += "{\n  \"version\": 1,\n  \"associations\": [";

    for (size_t i = 0; i < associations.size(); ++i) {
      const Association& a = associations[i];
      out += i == 0 ? "\n" : ",\n";
      out += "    {\n";

      auto key = [&out](const char* name) {
        out += "      \"";
        out += name;
        out += "\": ";
      };
      auto stringField = [&](const char* name, const std::string& value, bool last = false) {
        key(name);
        appendJsonString(out, value);
        out += last ? "\n" : ",\n";
      };
      auto numberField = [&](const char* name, int64_t value) {
        key(name);
        out += std::to_string(value);
        out += ",\n";
      };

      stringField("colormap_hash", toHex(a.hashes[Usage::Color]));
      stringField("normal_hash", toHex(a.hashes[Usage::Normal]));
      stringField("specular_hash", toHex(a.hashes[Usage::Specular]));
      stringField("height_hash", toHex(0));
      numberField("spec_channel_index", -1);
      numberField("normal_width", a.widths[Usage::Normal]);
      numberField("normal_height", a.heights[Usage::Normal]);
      numberField("specular_width", a.widths[Usage::Specular]);
      numberField("specular_height", a.heights[Usage::Specular]);
      numberField("height_width", 0);
      numberField("height_height", 0);
      stringField("shader_name", a.shaderName);
      stringField("colormap_name", a.names[Usage::Color]);
      stringField("normal_name", a.names[Usage::Normal]);
      stringField("specular_name", a.names[Usage::Specular]);
      stringField("height_name", std::string {});
      stringField("material_name", a.materialName);
      stringField("material_hash", toHex(a.material), true);

      out += "    }";
    }

    out += associations.empty() ? "]\n}\n" : "\n  ]\n}\n";
    return out;
  }

  // Minimal reader for the schema above. Unknown keys are skipped.
  class JsonReader {
  public:
    explicit JsonReader(const std::string& text)
      : m_p(text.data()), m_end(text.data() + text.size()) {
      if (m_end - m_p >= 3 && std::equal(m_p, m_p + 3, "\xEF\xBB\xBF")) {
        m_p += 3;
      }
    }

    bool parse(int& version, std::vector<Association>& out) {
      version = 0;
      if (!consume('{')) {
        return false;
      }
      if (consume('}')) {
        return true;
      }
      do {
        std::string key;
        if (!parseString(key) || !consume(':')) {
          return false;
        }
        if (key == "version") {
          double value = 0.0;
          if (!parseNumber(value)) {
            return false;
          }
          version = static_cast<int>(value);
        } else if (key == "associations") {
          if (!parseAssociations(out)) {
            return false;
          }
        } else if (!skipValue(0)) {
          return false;
        }
      } while (consume(','));
      return consume('}');
    }

  private:
    const char* m_p;
    const char* m_end;

    void skipWhitespace() {
      while (m_p < m_end && (*m_p == ' ' || *m_p == '\t' || *m_p == '\n' || *m_p == '\r')) {
        ++m_p;
      }
    }

    bool peek(char c) {
      skipWhitespace();
      return m_p < m_end && *m_p == c;
    }

    bool consume(char c) {
      if (!peek(c)) {
        return false;
      }
      ++m_p;
      return true;
    }

    static void appendUtf8(std::string& out, uint32_t cp) {
      if (cp < 0x80) {
        out += static_cast<char>(cp);
      } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
      } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
      } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
      }
    }

    bool parseHex4(uint32_t& out) {
      if (m_end - m_p < 4) {
        return false;
      }
      out = 0;
      for (int i = 0; i < 4; ++i, ++m_p) {
        const char c = *m_p;
        out <<= 4;
        if (c >= '0' && c <= '9') out |= c - '0';
        else if (c >= 'a' && c <= 'f') out |= c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') out |= c - 'A' + 10;
        else return false;
      }
      return true;
    }

    bool parseString(std::string& out) {
      if (!consume('"')) {
        return false;
      }
      out.clear();
      while (m_p < m_end) {
        const char c = *m_p++;
        if (c == '"') {
          return true;
        }
        if (c != '\\') {
          out += c;
          continue;
        }
        if (m_p >= m_end) {
          return false;
        }
        switch (*m_p++) {
        case '"': out += '"'; break;
        case '\\': out += '\\'; break;
        case '/': out += '/'; break;
        case 'b': out += '\b'; break;
        case 'f': out += '\f'; break;
        case 'n': out += '\n'; break;
        case 'r': out += '\r'; break;
        case 't': out += '\t'; break;
        case 'u': {
          uint32_t cp = 0;
          if (!parseHex4(cp)) {
            return false;
          }
          if (cp >= 0xDC00 && cp < 0xE000) {
            // Unpaired low surrogate.
            return false;
          }
          if (cp >= 0xD800 && cp < 0xDC00) {
            uint32_t low = 0;
            if (m_end - m_p < 6 || m_p[0] != '\\' || m_p[1] != 'u') {
              return false;
            }
            m_p += 2;
            if (!parseHex4(low) || low < 0xDC00 || low >= 0xE000) {
              return false;
            }
            cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
          }
          appendUtf8(out, cp);
          break;
        }
        default:
          return false;
        }
      }
      return false;
    }

    bool parseNumber(double& out) {
      skipWhitespace();
      const char* start = m_p;
      while (m_p < m_end && (std::isdigit(static_cast<unsigned char>(*m_p)) ||
                             *m_p == '-' || *m_p == '+' || *m_p == '.' || *m_p == 'e' || *m_p == 'E')) {
        ++m_p;
      }
      if (start == m_p) {
        return false;
      }
      const std::string text(start, m_p);
      char* end = nullptr;
      out = std::strtod(text.c_str(), &end);
      return end == text.c_str() + text.size();
    }

    bool skipLiteral(const char* literal) {
      const size_t length = std::strlen(literal);
      if (static_cast<size_t>(m_end - m_p) < length || std::strncmp(m_p, literal, length) != 0) {
        return false;
      }
      m_p += length;
      return true;
    }

    bool skipValue(int depth) {
      if (depth > 64) {
        return false;
      }
      skipWhitespace();
      if (m_p >= m_end) {
        return false;
      }
      std::string ignoredString;
      double ignoredNumber;
      switch (*m_p) {
      case '"':
        return parseString(ignoredString);
      case '{':
        ++m_p;
        if (consume('}')) {
          return true;
        }
        do {
          if (!parseString(ignoredString) || !consume(':') || !skipValue(depth + 1)) {
            return false;
          }
        } while (consume(','));
        return consume('}');
      case '[':
        ++m_p;
        if (consume(']')) {
          return true;
        }
        do {
          if (!skipValue(depth + 1)) {
            return false;
          }
        } while (consume(','));
        return consume(']');
      case 't': return skipLiteral("true");
      case 'f': return skipLiteral("false");
      case 'n': return skipLiteral("null");
      default:
        return parseNumber(ignoredNumber);
      }
    }

    bool parseAssociation(Association& a) {
      if (!consume('{')) {
        return false;
      }
      if (consume('}')) {
        return true;
      }
      bool hasMaterialHash = false;
      do {
        std::string key;
        if (!parseString(key) || !consume(':')) {
          return false;
        }

        if (peek('"')) {
          std::string value;
          if (!parseString(value)) {
            return false;
          }
          XXH64_hash_t hash = 0;
          if (key == "colormap_hash" && parseHex(value, hash)) a.hashes[Usage::Color] = hash;
          else if (key == "normal_hash" && parseHex(value, hash)) a.hashes[Usage::Normal] = hash;
          else if (key == "specular_hash" && parseHex(value, hash)) a.hashes[Usage::Specular] = hash;
          else if (key == "material_hash" && parseHex(value, hash)) { a.material = hash; hasMaterialHash = true; }
          else if (key == "shader_name") a.shaderName = std::move(value);
          else if (key == "colormap_name") a.names[Usage::Color] = std::move(value);
          else if (key == "normal_name") a.names[Usage::Normal] = std::move(value);
          else if (key == "specular_name") a.names[Usage::Specular] = std::move(value);
          else if (key == "material_name") a.materialName = std::move(value);
        } else if (key == "normal_width" || key == "normal_height" ||
                   key == "specular_width" || key == "specular_height") {
          double value = 0.0;
          if (!parseNumber(value)) {
            return false;
          }
          const uint32_t size = value > 0.0 ? static_cast<uint32_t>(value) : 0u;
          if (key == "normal_width") a.widths[Usage::Normal] = size;
          else if (key == "normal_height") a.heights[Usage::Normal] = size;
          else if (key == "specular_width") a.widths[Usage::Specular] = size;
          else a.heights[Usage::Specular] = size;
        } else if (!skipValue(1)) {
          return false;
        }
      } while (consume(','));

      // Files without material_hash: the key of a D3D9 draw without RS overrides.
      if (!hasMaterialHash) {
        a.material = a.hashes[Usage::Color];
      }
      return consume('}');
    }

    bool parseAssociations(std::vector<Association>& out) {
      if (!consume('[')) {
        return false;
      }
      if (consume(']')) {
        return true;
      }
      do {
        Association a;
        if (!parseAssociation(a)) {
          return false;
        }
        if (a.material != kEmptyHash) {
          out.push_back(std::move(a));
        }
      } while (consume(','));
      return consume(']');
    }
  };

  // ---------------------------------------------------------------------------
  // comp_world_autopbr.usda / comp_mesh_autopbr.usda

  std::string escapeUsdaString(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    for (const char c : value) {
      if (c == '"' || c == '\\') {
        out += '\\';
      }
      out += c;
    }
    return out;
  }

  std::string serializeUsda(const std::vector<Association>& associations, const char* category) {
    std::string out =
      "#usda 1.0\n"
      "(\n"
      "    upAxis = \"Z\"\n"
      ")\n"
      "\n"
      "over \"RootNode\"\n"
      "{\n"
      "    over \"Looks\"\n"
      "    {\n";

    bool first = true;
    for (const Association& a : associations) {
      const XXH64_hash_t normal = a.hashes[Usage::Normal];
      const XXH64_hash_t specular = a.hashes[Usage::Specular];
      if (normal == kEmptyHash && specular == kEmptyHash) {
        continue;
      }
      const MaterialLocation location = materialLocation(a);
      if (std::strcmp(location.category, category) != 0) {
        continue;
      }
      const std::string assetDir = std::string("./assets/") + category + "/" + location.folder + "/";
      if (!first) {
        out += "\n";
      }
      first = false;

      out += "        over \"mat_" + toHex(a.material) + "\"\n";
      out += "        {\n";
      if (!a.materialName.empty()) {
        out += "            custom string nickname = \"" + escapeUsdaString(a.materialName) + "\"\n";
      }
      out += "            over \"Shader\"\n";
      out += "            {\n";
      if (normal != kEmptyHash) {
        out += "                asset inputs:normalmap_texture = @" + assetDir + "normal_oth.dds@\n";
      }
      if (specular != kEmptyHash) {
        out += "                asset inputs:reflectionroughness_texture = @" + assetDir + "roughness.dds@\n";
      }
      out += "            }\n";
      out += "        }\n";
    }

    out += "    }\n}\n";
    return out;
  }

  // ---------------------------------------------------------------------------
  // State helpers (callers hold State::mutex)

  // Fills slots and names the stored entry is missing. Returns true on change.
  bool mergeAssociation(Association& dst, const Association& src) {
    bool changed = false;
    for (uint32_t u = 0; u < Usage::Count; ++u) {
      if (dst.hashes[u] == kEmptyHash && src.hashes[u] != kEmptyHash) {
        dst.hashes[u] = src.hashes[u];
        dst.widths[u] = src.widths[u];
        dst.heights[u] = src.heights[u];
        changed = true;
      }
      if (dst.names[u].empty() && !src.names[u].empty() && dst.hashes[u] == src.hashes[u]) {
        dst.names[u] = src.names[u];
        changed = true;
      }
    }
    if (dst.shaderName.empty() && !src.shaderName.empty()) {
      dst.shaderName = src.shaderName;
      changed = true;
    }
    // Names from older files lack the category prefix; the first prefixed name replaces them.
    if (!src.materialName.empty() && dst.materialName != src.materialName &&
        (dst.materialName.empty() || (!hasCategoryPrefix(dst.materialName) && hasCategoryPrefix(src.materialName)))) {
      dst.materialName = src.materialName;
      changed = true;
    }
    return changed;
  }

  bool upsertAssociation(State& s, const Association& a) {
    auto it = s.index.find(a.material);
    if (it == s.index.end()) {
      s.index.emplace(a.material, s.associations.size());
      s.associations.push_back(a);
      return true;
    }
    return mergeAssociation(s.associations[it->second], a);
  }

  void queueExport(State& s, std::filesystem::path path, const game_textures::Texture& texture) {
    if (texture.isValid() && s.known.insert(path.generic_string()).second) {
      s.queue.push_back(ExportJob { std::move(path), texture.view });
      s_hasWork = true;
    }
  }

  // Known dumps = files on disk (dump/<category>/<folder>/*.dds) + queued exports,
  // so deleting the dump folder makes the next collection write everything again.
  void seedKnownFromDisk() {
    std::vector<std::string> found;
    auto forEachDir = [](const std::filesystem::path& dir, auto&& callback) {
      std::error_code ec;
      std::filesystem::directory_iterator it(dir, ec);
      for (; !ec && it != std::filesystem::directory_iterator(); it.increment(ec)) {
        callback(it->path());
      }
    };
    forEachDir(imgdumpDir() / kDumpDir, [&](const std::filesystem::path& category) {
      forEachDir(category, [&](const std::filesystem::path& folder) {
        forEachDir(folder, [&](const std::filesystem::path& file) {
          if (file.extension() == ".dds") {
            found.push_back(file.generic_string());
          }
        });
      });
    });

    State& s = state();
    std::lock_guard lock { s.mutex };
    s.known.clear();
    s.known.insert(found.begin(), found.end());
    for (const ExportJob& job : s.queue) {
      s.known.insert(job.path.generic_string());
    }
  }

  void setStatus(std::string status) {
    Logger::info("[AutoPBR] " + status);
    State& s = state();
    std::lock_guard lock { s.mutex };
    s.status = std::move(status);
  }

  void setLoaded() {
    State& s = state();
    std::lock_guard lock { s.mutex };
    s.loaded = true;
  }

  // Writes associations.json / usda snapshots on one owned worker thread so the
  // render / UI thread never waits on the disk. A queued snapshot is replaced
  // by a newer one for the same file. flush() drains the queue and joins.
  class FileWriter {
  public:
    // usdaCategory selects the usda for that category; nullptr writes associations.json.
    void write(std::filesystem::path path, std::vector<Association> snapshot, const char* usdaCategory) {
      std::lock_guard lock { m_mutex };
      auto it = std::find_if(m_jobs.begin(), m_jobs.end(), [&path](const Job& job) { return job.path == path; });
      if (it != m_jobs.end()) {
        it->snapshot = std::move(snapshot);
      } else {
        m_jobs.push_back(Job { std::move(path), std::move(snapshot), usdaCategory });
      }
      if (!m_thread.joinable()) {
        m_stop = false;
        m_thread = std::thread([this] { run(); });
      }
      m_cond.notify_one();
    }

    void flush() {
      std::thread thread;
      {
        std::lock_guard lock { m_mutex };
        m_stop = true;
        thread = std::move(m_thread);
      }
      m_cond.notify_one();
      if (thread.joinable()) {
        thread.join();
      }
    }

  private:
    struct Job {
      std::filesystem::path path;
      std::vector<Association> snapshot;
      const char* usdaCategory = nullptr;
    };

    std::mutex m_mutex;
    std::condition_variable m_cond;
    std::deque<Job> m_jobs;
    std::thread m_thread;
    bool m_stop = false;

    void run() {
      while (true) {
        Job job;
        {
          std::unique_lock lock { m_mutex };
          m_cond.wait(lock, [this] { return m_stop || !m_jobs.empty(); });
          if (m_jobs.empty()) {
            return;
          }
          job = std::move(m_jobs.front());
          m_jobs.pop_front();
        }

        const std::string contents = job.usdaCategory != nullptr
          ? serializeUsda(job.snapshot, job.usdaCategory)
          : serializeAssociations(job.snapshot);
        bool written;
        {
          std::lock_guard fileLock { s_fileMutex };
          written = writeFileAtomic(job.path, contents);
        }
        if (!written) {
          setStatus("Writing " + job.path.string() + " failed.");
        } else if (job.usdaCategory != nullptr) {
          setStatus("Wrote " + job.path.string() + ".");
        } else {
          setStatus(str::format("Saved ", job.snapshot.size(), " associations."));
        }
      }
    }
  };

  // Never destroyed: a static destructor would join the worker during DLL
  // unload. AutoPbr::reset() flushes it on device / API teardown instead.
  FileWriter& fileWriter() {
    static FileWriter* s_writer = new FileWriter();
    return *s_writer;
  }

  void writeFileAsync(std::filesystem::path path, std::vector<Association> snapshot, const char* usdaCategory) {
    fileWriter().write(std::move(path), std::move(snapshot), usdaCategory);
  }

  void saveAssociations() {
    State& s = state();
    std::vector<Association> snapshot;
    {
      std::lock_guard lock { s.mutex };
      snapshot = s.associations;
      s.unsaved = 0;
    }
    writeFileAsync(imgdumpDir() / kAssociationsFile, std::move(snapshot), nullptr);
  }

  void writeUsda() {
    State& s = state();
    std::vector<Association> snapshot;
    {
      std::lock_guard lock { s.mutex };
      snapshot = s.associations;
    }
    writeFileAsync(imgdumpDir() / kWorldUsdaFile, snapshot, kWorldCategory);
    writeFileAsync(imgdumpDir() / kMeshUsdaFile, std::move(snapshot), kMeshCategory);
  }

  enum class LoadResult {
    Loaded,
    Missing,
    // Unreadable file, moved aside so a later save cannot overwrite it.
    MovedAside,
    // Unreadable file that could not be moved aside.
    Failed,
  };

  // Caller holds s_fileMutex. Returns the backup path, empty on failure.
  std::filesystem::path moveAside(const std::filesystem::path& path) {
    std::error_code ec;
    std::filesystem::path backup = path;
    backup += ".bak";
    for (uint32_t i = 1; std::filesystem::exists(backup, ec) && i < 1000; ++i) {
      backup = path;
      backup += ".bak" + std::to_string(i);
    }
    std::filesystem::rename(path, backup, ec);
    return ec ? std::filesystem::path {} : backup;
  }

  LoadResult loadAssociations() {
    const std::filesystem::path path = imgdumpDir() / kAssociationsFile;
    int version = 0;
    std::vector<Association> loaded;
    {
      std::lock_guard fileLock { s_fileMutex };
      std::ifstream file(path, std::ios::binary);
      if (!file) {
        setStatus("No " + path.string() + " to load.");
        return LoadResult::Missing;
      }
      std::ostringstream buffer;
      buffer << file.rdbuf();
      file.close();

      if (!JsonReader(buffer.str()).parse(version, loaded) || version != 1) {
        const std::filesystem::path backup = moveAside(path);
        if (backup.empty()) {
          setStatus("Failed to parse " + path.string() + " (expected version 1) and could not move it aside.");
          return LoadResult::Failed;
        }
        setLoaded();
        setStatus("Failed to parse " + path.string() + " (expected version 1); moved it to " + backup.string() + ".");
        return LoadResult::MovedAside;
      }
    }

    size_t added = 0;
    {
      State& s = state();
      std::lock_guard lock { s.mutex };
      for (const Association& a : loaded) {
        if (upsertAssociation(s, a)) {
          ++added;
        }
      }
      s.loaded = true;
    }
    seedKnownFromDisk();
    setStatus(str::format("Loaded ", loaded.size(), " associations (", added, " new or updated)."));
    return LoadResult::Loaded;
  }

  void clearAssociations() {
    State& s = state();
    std::lock_guard lock { s.mutex };
    s.associations.clear();
    s.index.clear();
    s.known.clear();
    s.queue.clear();
    s_hasWork = false;
    s.unsaved = 0;
    s.loaded = true;
    s.status = "Cleared associations.";
  }

  // ---------------------------------------------------------------------------
  // Texture export

  bool isOwnComponent(VkComponentSwizzle swizzle, VkComponentSwizzle self) {
    return swizzle == VK_COMPONENT_SWIZZLE_IDENTITY || swizzle == self;
  }

  bool hasSwizzle(const VkComponentMapping& m) {
    return !isOwnComponent(m.r, VK_COMPONENT_SWIZZLE_R) || !isOwnComponent(m.g, VK_COMPONENT_SWIZZLE_G) ||
           !isOwnComponent(m.b, VK_COMPONENT_SWIZZLE_B) || !isOwnComponent(m.a, VK_COMPONENT_SWIZZLE_A);
  }

  bool supportsSwizzledReadback(VkFormat format) {
    switch (format) {
    case VK_FORMAT_R8_UNORM:
    case VK_FORMAT_R8G8_UNORM:
    case VK_FORMAT_R8G8B8A8_UNORM:
    case VK_FORMAT_B8G8R8A8_UNORM:
      return true;
    default:
      return false;
    }
  }

  void readTexel(VkFormat format, const uint8_t* src, size_t i, uint8_t rgba[4]) {
    switch (format) {
    case VK_FORMAT_R8_UNORM:
      rgba[0] = src[i]; rgba[1] = 0; rgba[2] = 0; rgba[3] = 255;
      break;
    case VK_FORMAT_R8G8_UNORM:
      rgba[0] = src[2 * i]; rgba[1] = src[2 * i + 1]; rgba[2] = 0; rgba[3] = 255;
      break;
    case VK_FORMAT_B8G8R8A8_UNORM:
      rgba[0] = src[4 * i + 2]; rgba[1] = src[4 * i + 1]; rgba[2] = src[4 * i]; rgba[3] = src[4 * i + 3];
      break;
    default:
      std::memcpy(rgba, src + 4 * i, 4);
      break;
    }
  }

  uint8_t applySwizzle(VkComponentSwizzle swizzle, uint32_t channel, const uint8_t rgba[4]) {
    switch (swizzle) {
    case VK_COMPONENT_SWIZZLE_IDENTITY: return rgba[channel];
    case VK_COMPONENT_SWIZZLE_ZERO: return 0;
    case VK_COMPONENT_SWIZZLE_ONE: return 255;
    default: return rgba[swizzle - VK_COMPONENT_SWIZZLE_R];
    }
  }

  // Exporter thread. Writes the texture as the shader sees it (D3D9 view
  // swizzle applied), e.g. L8 -> LLL1, A8L8 -> LLLA, X8R8G8B8 -> RGB1.
  void writeSwizzledDds(
      const std::string& path,
      const uint8_t* data,
      VkFormat format,
      const VkComponentMapping& mapping,
      const std::vector<VkExtent3D>& extents,
      const std::vector<VkDeviceSize>& offsets) {
    gli::texture2d texture(gli::FORMAT_RGBA8_UNORM_PACK8,
                           gli::extent2d(extents[0].width, extents[0].height),
                           extents.size());
    const VkComponentSwizzle swizzles[4] = { mapping.r, mapping.g, mapping.b, mapping.a };

    for (size_t level = 0; level < extents.size(); ++level) {
      const uint8_t* src = data + offsets[level];
      uint8_t* dst = static_cast<uint8_t*>(texture.data(0, 0, level));
      const size_t texelCount = size_t(extents[level].width) * extents[level].height;

      for (size_t i = 0; i < texelCount; ++i) {
        uint8_t rgba[4];
        readTexel(format, src, i, rgba);
        for (uint32_t c = 0; c < 4; ++c) {
          dst[4 * i + c] = applySwizzle(swizzles[c], c, rgba);
        }
      }
    }

    if (!gli::save(texture, path)) {
      Logger::err(str::format("[AutoPBR] Failed to write texture \"", path, "\""));
    }
  }

  // AssetExporter::exportImage copies raw image data, which loses the D3D9
  // view swizzle (L8 would come out red-only). Read the mips back and apply it.
  void exportSwizzled(const Rc<DxvkContext>& ctx, const Rc<DxvkImageView>& view, const std::string& path) {
    const Rc<DxvkImage> image = view->image();
    const VkFormat format = image->info().format;
    const VkDeviceSize elementSize = imageFormatInfo(format)->elementSize;
    const uint32_t mipCount = image->info().mipLevels;

    std::vector<VkExtent3D> extents(mipCount);
    std::vector<VkDeviceSize> offsets(mipCount);
    VkDeviceSize size = 0;
    for (uint32_t mip = 0; mip < mipCount; ++mip) {
      extents[mip] = image->mipLevelExtent(mip);
      offsets[mip] = size;
      size += align(VkDeviceSize(extents[mip].width) * extents[mip].height * elementSize, VkDeviceSize(16));
    }

    DxvkBufferCreateInfo info = {};
    info.size = size;
    info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    info.stages = VK_PIPELINE_STAGE_TRANSFER_BIT;
    info.access = VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
    Rc<DxvkBuffer> buffer = ctx->getDevice()->createBuffer(
      info, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, DxvkMemoryStats::Category::RTXBuffer, "AutoPBR readback");

    for (uint32_t mip = 0; mip < mipCount; ++mip) {
      ctx->copyImageToBuffer(buffer, offsets[mip], 0, 0, image,
                             VkImageSubresourceLayers { VK_IMAGE_ASPECT_COLOR_BIT, mip, 0, 1 },
                             VkOffset3D { 0, 0, 0 }, extents[mip]);
    }

    ctx->getCommonObjects()->metaExporter().copyBufferFromGPU(ctx, DxvkBufferSlice(buffer),
      [path, format, mapping = view->info().swizzle, extents, offsets](Rc<DxvkBuffer> readback) {
        writeSwizzledDds(path, static_cast<const uint8_t*>(readback->mapPtr(0)), format, mapping, extents, offsets);
      });
  }

  // Returns true when an export was issued.
  bool exportTexture(const Rc<DxvkContext>& ctx, const ExportJob& job) {
    const std::filesystem::path dir = job.path.parent_path();
    const std::string fileName = job.path.filename().string();

    std::error_code ec;
    if (std::filesystem::exists(job.path, ec)) {
      return false;
    }
    std::filesystem::create_directories(dir, ec);

    const Rc<DxvkImage> image = job.view->image();
    if (image->info().type != VK_IMAGE_TYPE_2D || image->info().numLayers != 1) {
      return false;
    }

    if (hasSwizzle(job.view->info().swizzle)) {
      if (supportsSwizzledReadback(image->info().format)) {
        exportSwizzled(ctx, job.view, job.path.string());
        return true;
      }
      const int format = static_cast<int>(image->info().format);
      ONCE(Logger::warn(str::format("[AutoPBR] Exporting VkFormat ", format, " without its D3D9 view swizzle.")));
    }

    ctx->getCommonObjects()->metaExporter().dumpImageToFile(ctx, dir.string() + "/", fileName, image);
    return true;
  }

} // anonymous namespace

  bool AutoPbr::isCollecting() {
    return s_collecting.load(std::memory_order_relaxed);
  }

  void AutoPbr::setCollecting(bool collecting) {
    if (collecting == isCollecting()) {
      return;
    }

    if (!collecting) {
      s_collecting = false;
      fork_game_state::GameStateStore::get().set(kCollectingKey, "0");
      bool unsaved;
      {
        State& s = state();
        std::lock_guard lock { s.mutex };
        unsaved = s.unsaved > 0;
      }
      if (unsaved) {
        saveAssociations();
      }
      setStatus("Stopped collecting.");
      return;
    }

    seedKnownFromDisk();

    // Resume an earlier session instead of overwriting its file on the next save.
    bool loaded;
    {
      State& s = state();
      std::lock_guard lock { s.mutex };
      loaded = s.loaded;
    }
    std::error_code ec;
    if (!loaded && std::filesystem::exists(imgdumpDir() / kAssociationsFile, ec) &&
        loadAssociations() == LoadResult::Failed) {
      // Collecting would autosave over the unreadable file; the status says why.
      return;
    }

    s_collecting = true;
    fork_game_state::GameStateStore::get().set(kCollectingKey, "1");
    // NORMAL / SPECULAR of materials created while idle resolve on the API thread.
    game_textures::requestDeferredResolve();

    // API materials are typically created at level load, before collecting starts.
    std::vector<LiveMaterial> live;
    {
      State& s = state();
      std::lock_guard lock { s.mutex };
      live.reserve(s.liveMaterials.size());
      for (const auto& entry : s.liveMaterials) {
        live.push_back(entry.second);
      }
    }
    for (const LiveMaterial& material : live) {
      addAssociation(material.key, *material.textures);
    }
    setStatus(str::format("Collecting (", live.size(), " API materials picked up)."));
  }

  void AutoPbr::addAssociation(
      XXH64_hash_t materialHash,
      const game_textures::TextureSet& textures,
      const game_textures::Texture* fallbackColor) {
    if (!isCollecting() || materialHash == kEmptyHash) {
      return;
    }

    const game_textures::Texture* slots[Usage::Count] = {
      textures.color().isValid() ? &textures.color() : fallbackColor,
      &textures.textures[Usage::Normal],
      &textures.textures[Usage::Specular],
    };

    Association a;
    a.material = materialHash;
    a.shaderName = textures.shaderName;
    a.materialName = textures.materialName;
    for (uint32_t u = 0; u < Usage::Count; ++u) {
      if (slots[u] != nullptr && slots[u]->isValid()) {
        a.hashes[u] = slots[u]->hash;
        a.widths[u] = slots[u]->width;
        a.heights[u] = slots[u]->height;
        a.names[u] = slots[u]->name;
      }
    }

    const std::filesystem::path dumpRoot = imgdumpDir() / kDumpDir;
    State& s = state();
    std::lock_guard lock { s.mutex };
    if (upsertAssociation(s, a)) {
      ++s.unsaved;
      s_hasWork = true;
    }

    // Dump into the stored association's folder, only textures it recorded.
    const Association& stored = s.associations[s.index.at(materialHash)];
    const MaterialLocation location = materialLocation(stored);
    const std::filesystem::path dir = dumpRoot / location.category / location.folder;
    for (uint32_t u = 0; u < Usage::Count; ++u) {
      if (slots[u] != nullptr && slots[u]->isValid() && slots[u]->hash == stored.hashes[u]) {
        queueExport(s, dir / kUsageFiles[u], *slots[u]);
      }
    }
  }

  void AutoPbr::registerApiMaterial(
      remixapi_MaterialHandle handle,
      XXH64_hash_t materialHash,
      std::shared_ptr<const game_textures::TextureSet> textures) {
    {
      State& s = state();
      std::lock_guard lock { s.mutex };
      s.liveMaterials[handle] = LiveMaterial { materialHash, textures };
    }
    addAssociation(materialHash, *textures);
  }

  void AutoPbr::unregisterApiMaterial(remixapi_MaterialHandle handle) {
    State& s = state();
    std::lock_guard lock { s.mutex };
    s.liveMaterials.erase(handle);
  }

  void AutoPbr::completeApiMaterial(remixapi_MaterialHandle handle, const game_textures::TextureSet& textures) {
    LiveMaterial updated;
    {
      State& s = state();
      std::lock_guard lock { s.mutex };
      auto it = s.liveMaterials.find(handle);
      if (it == s.liveMaterials.end()) {
        return;
      }
      auto merged = std::make_shared<game_textures::TextureSet>(*it->second.textures);
      for (uint32_t u = 0; u < Usage::Count; ++u) {
        if (!merged->textures[u].isValid() && textures.textures[u].isValid()) {
          merged->textures[u] = textures.textures[u];
        }
      }
      it->second.textures = merged;
      updated = it->second;
    }
    addAssociation(updated.key, *updated.textures);
  }

  void AutoPbr::setTexturesWaitingForData(uint32_t count) {
    s_waitingForData = count;
  }

  void AutoPbr::reset() {
    bool unsaved;
    {
      State& s = state();
      std::lock_guard lock { s.mutex };
      s.queue.clear();
      s.liveMaterials.clear();
      unsaved = s.unsaved > 0;
      s_hasWork = false;
    }
    s_waitingForData = 0;
    if (unsaved) {
      saveAssociations();
    }
    fileWriter().flush();
  }

  void AutoPbr::endFrame(RtxContext& ctx) {
    if (!s_hasWork.load(std::memory_order_relaxed)) {
      return;
    }

    State& s = state();
    std::vector<ExportJob> jobs;
    bool autosave = false;
    {
      std::lock_guard lock { s.mutex };
      const size_t budget = static_cast<size_t>(std::max(1, exportsPerFrame()));
      while (!s.queue.empty() && jobs.size() < budget) {
        jobs.push_back(std::move(s.queue.front()));
        s.queue.pop_front();
      }
      const int interval = autosaveInterval();
      autosave = interval > 0 && s.unsaved >= static_cast<uint32_t>(interval);
      s_hasWork = !s.queue.empty();
    }

    const Rc<DxvkContext> context(&ctx);
    uint64_t exported = 0;
    for (const ExportJob& job : jobs) {
      if (exportTexture(context, job)) {
        ++exported;
      }
    }

    if (exported > 0) {
      std::lock_guard lock { s.mutex };
      s.exported += exported;
    }
    if (autosave) {
      saveAssociations();
    }
  }

  void AutoPbr::showImguiSettings() {
    if (!RemixGui::CollapsingHeader("AutoPBR")) {
      return;
    }
    ImGui::Indent();

    ImGui::TextWrapped("Collects the game normal / specular textures of every material, dumps them to "
                       "dump/<wc|mc>/<material>/ and writes associations.json + comp_world_autopbr.usda / "
                       "comp_mesh_autopbr.usda for the conversion scripts.");
    ImGui::TextWrapped("Output: %s", imgdumpDir().string().c_str());

    const bool collecting = isCollecting();
    if (IMGUI_ADD_TOOLTIP(ImGui::Button(collecting ? "Stop Collecting" : "Start Collecting"),
                          "Starting resumes an existing associations.json and skips textures already on disk. "
                          "The game is notified through __autopbr.collecting.")) {
      setCollecting(!collecting);
    }
    ImGui::SameLine();
    ImGui::TextUnformatted(collecting ? "Collecting" : "Idle");

    if (IMGUI_ADD_TOOLTIP(ImGui::Button("Save Associations"), "Writes associations.json.")) {
      saveAssociations();
    }
    ImGui::SameLine();
    if (IMGUI_ADD_TOOLTIP(ImGui::Button("Load Associations"), "Merges associations.json into the current associations.")) {
      loadAssociations();
    }
    ImGui::SameLine();
    if (IMGUI_ADD_TOOLTIP(ImGui::Button("Clear Associations"),
                          "Clears the associations in memory. Files on disk are kept.")) {
      clearAssociations();
    }
    if (IMGUI_ADD_TOOLTIP(ImGui::Button("Write USDA"),
                          "Writes comp_world_autopbr.usda (wc/) and comp_mesh_autopbr.usda (mc/) for every material "
                          "with a normal or specular texture.")) {
      writeUsda();
    }

    size_t materials = 0, withMaps = 0, queued = 0, onDisk = 0;
    uint64_t exported = 0;
    std::string status;
    {
      State& s = state();
      std::lock_guard lock { s.mutex };
      materials = s.associations.size();
      for (const Association& a : s.associations) {
        if (a.hashes[Usage::Normal] != kEmptyHash || a.hashes[Usage::Specular] != kEmptyHash) {
          ++withMaps;
        }
      }
      queued = s.queue.size();
      onDisk = s.known.size() - std::min(s.known.size(), queued);
      exported = s.exported;
      status = s.status;
    }

    ImGui::Text("Materials: %zu (%zu with normal / specular)", materials, withMaps);
    ImGui::Text("Textures dumped: %llu this session, %zu known on disk, %zu queued",
                static_cast<unsigned long long>(exported), onDisk, queued);
    ImGui::Text("API material textures waiting for data: %u", s_waitingForData.load());
    if (!status.empty()) {
      ImGui::TextWrapped("%s", status.c_str());
    }

    RemixGui::DragInt("Exports Per Frame", &exportsPerFrameObject(), 0.1f, 1, 256);
    RemixGui::DragInt("Autosave Interval", &autosaveIntervalObject(), 1.0f, 0, 10000);

    ImGui::Unindent();
  }

namespace fork_hooks {

  void autoPbrEndFrame(RtxContext& ctx) {
    AutoPbr::endFrame(ctx);
  }

  void showAutoPbrUI() {
    AutoPbr::showImguiSettings();
  }

} // namespace fork_hooks
} // namespace dxvk
