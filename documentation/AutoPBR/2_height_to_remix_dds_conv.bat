:: Add NVTT to your PATH or use:
:: cd /d G:\Programme\NVIDIA Texture Tools
:: nvcompress.exe

nvcompress -bc4 -no-mip-gamma-correct -mip-filter max "height_out" "autoconv"
pause