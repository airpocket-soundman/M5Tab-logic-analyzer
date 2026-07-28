# ---------------------------------------------------------------------------
#  build_shots.ps1 - regenerate the documentation screenshots
# ---------------------------------------------------------------------------
#
#  Compiles the simulator sources plus sim/tools/shotgen.cpp to a Node program
#  with the same Emscripten toolchain the web simulator uses, runs it, and
#  converts the BMPs it emits into PNGs under docs/img.
#
#  Usage:  pwsh sim/tools/build_shots.ps1 [path-to-web-simulator-for-lvgl]
# ---------------------------------------------------------------------------
param(
  [string]$Simulator = "D:\github\airpocket-soundman\web-simulator-for-lvgl"
)

$ErrorActionPreference = "Stop"
$project = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$emcc = Join-Path $Simulator ".sdk\emsdk\upstream\emscripten\emcc.bat"
if (-not (Test-Path $emcc)) { throw "Emscripten not found at $emcc" }

$outDir = Join-Path $project "build\shots"
$imgDir = Join-Path $project "docs\img"
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
New-Item -ItemType Directory -Force -Path $imgDir | Out-Null

$js = Join-Path $outDir "shotgen.js"
$args = @(
  '-O1', '-sNODERAWFS=1', '-sALLOW_MEMORY_GROWTH=1', '-sEXIT_RUNTIME=1',
  "-I$Simulator\include", '-DM5GFX_WEB_SIMULATOR=1',
  "-I$project\sim\shim", "-I$project\sim", "-I$project\include", "-I$project\src",
  '-DLA_SIMULATOR=1',
  "$project\sim\tools\shotgen.cpp",
  "$project\sim\sim_main.cpp", "$project\sim\sim_gfx.cpp",
  "$project\sim\sim_sampler.cpp", "$project\sim\sim_exporter.cpp",
  "$project\src\logic_types.cpp", "$project\src\capture\capture_buffer.cpp",
  "$project\src\analysis\trigger.cpp", "$project\src\analysis\measure.cpp",
  "$project\src\decode\decoder.cpp", "$project\src\ui\waveform_view.cpp",
  "$project\src\app.cpp", "$project\src\api\serial_api.cpp",
  '-o', $js
)
& $emcc @args
if ($LASTEXITCODE -ne 0) { throw "emcc failed" }

$node = Join-Path $Simulator ".sdk\emsdk\node\22.16.0_64bit\bin\node.exe"
if (-not (Test-Path $node)) { $node = "node" }
& $node $js $outDir
if ($LASTEXITCODE -ne 0) { throw "shotgen failed" }

python "$project\sim\tools\to_png.py" $outDir $imgDir
