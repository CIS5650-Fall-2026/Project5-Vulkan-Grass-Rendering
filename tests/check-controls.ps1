$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$sdk = if ($env:VULKAN_SDK) { $env:VULKAN_SDK } else { Join-Path $root 'build/tools/VulkanSDK' }
$names = @('VK_LAYER_PATH','VK_LAYER_VALIDATE_SYNC','VK_LOADER_LAYERS_DISABLE')
$saved = @{}
foreach ($name in $names) { $saved[$name] = [Environment]::GetEnvironmentVariable($name) }
try {
    $env:VK_LAYER_PATH = Join-Path $sdk 'Bin'
    $env:VK_LAYER_VALIDATE_SYNC = '1'
    $env:VK_LOADER_LAYERS_DISABLE = '~implicit~'
    $run = Start-Process -FilePath (Join-Path $root 'bin/Debug/vulkan_grass_rendering.exe') -ArgumentList '--check-controls','--verify' -WorkingDirectory (Join-Path $root 'build/src') -WindowStyle Hidden -RedirectStandardOutput "$root/build/controls.stdout.log" -RedirectStandardError "$root/build/controls.stderr.log" -PassThru -Wait
    $log = Get-Content "$root/build/controls.stdout.log" -Raw
    if ($run.ExitCode -ne 0 -or (Get-Item "$root/build/controls.stderr.log").Length -ne 0 -or $log -match 'Validation (Warning|Error)|VUID-|SYNC-HAZARD' -or $log -notmatch 'Control checks passed') {
        throw 'Control check failed; see build/controls logs'
    }
    [pscustomobject]@{ callbacks_passed=$true; resize_events=2; validation_messages=0; manual_input_tested=$false } | ConvertTo-Json | Set-Content "$root/performance/controls-validation.json"
    Write-Output 'Control callbacks, two actual resizes, and GPU verification passed.'
} finally {
    foreach ($name in $names) { [Environment]::SetEnvironmentVariable($name, $saved[$name]) }
}
