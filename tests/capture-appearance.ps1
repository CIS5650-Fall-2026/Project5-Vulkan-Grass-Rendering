param([string]$Configuration = 'Debug')
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$output = Join-Path $root 'build/appearance'
New-Item -ItemType Directory -Force $output | Out-Null
$cases = @(
    @{ Name='wind-controls'; Arguments=@('--wind-demo','--view','close','--frames','180','--warmup','0','--capture-every','6') },
    @{ Name='lighting'; Arguments=@('--view','close','--orbit','--frames','180','--warmup','60','--capture-every','4') },
    @{ Name='calm'; Arguments=@('--wind','0','--view','close','--frames','120','--warmup','119') },
    @{ Name='reverse'; Arguments=@('--wind','20','--wind-direction','180','--view','close','--frames','120','--warmup','119') },
    @{ Name='sky'; Arguments=@('--view','away','--frames','2','--warmup','1') }
)
$lines = @()
foreach ($case in $cases) {
    $directory = Join-Path $output $case.Name
    New-Item -ItemType Directory -Force $directory | Out-Null
    $arguments = @('--capture-dir',$directory,'--fixed-dt','0.016666667','--verify','--no-collider') + $case.Arguments
    $lines += (@("$output/$($case.Name).stdout.log","$output/$($case.Name).stderr.log") + $arguments) -join "`t"
}
# Exercise the ordinary swapchain too, after the offscreen captures.
$lines += (@("$output/presented.stdout.log","$output/presented.stderr.log",'--frames','3','--verify') -join "`t")
$batch = Join-Path $output 'captures.tsv'
[IO.File]::WriteAllLines($batch, $lines, [Text.UTF8Encoding]::new($false))
$sdk = if ($env:VULKAN_SDK) { $env:VULKAN_SDK } else { Join-Path $root 'build/tools/VulkanSDK' }
$names = @('VK_LAYER_PATH','VK_LAYER_ENABLES','VK_LAYER_VALIDATE_SYNC','VK_INSTANCE_LAYERS','VK_LOADER_LAYERS_DISABLE')
$saved = @{}
foreach ($name in $names) { $saved[$name] = [Environment]::GetEnvironmentVariable($name) }
try {
    $env:VK_LAYER_PATH = Join-Path $sdk 'Bin'
    $env:VK_LAYER_ENABLES = $null
    $env:VK_LAYER_VALIDATE_SYNC = '1'
    $env:VK_INSTANCE_LAYERS = 'VK_LAYER_KHRONOS_validation'
    $env:VK_LOADER_LAYERS_DISABLE = '~implicit~'
    $run = Start-Process -FilePath (Join-Path $root "bin/$Configuration/vulkan_grass_rendering.exe") -ArgumentList '--batch', ('"' + $batch + '"') -WorkingDirectory (Join-Path $root 'build/src') -WindowStyle Hidden -PassThru -Wait
    if ($run.ExitCode -ne 0) { throw 'Capture failed; see build/appearance logs' }
    foreach ($name in (@($cases | ForEach-Object { $_.Name }) + @('presented'))) {
        if ((Get-Item "$output/$name.stderr.log").Length -ne 0) { throw "Validation error in $name" }
        $text = Get-Content "$output/$name.stdout.log" -Raw
        if ($text -match 'Validation (Warning|Error)|VUID-|SYNC-HAZARD') { throw "Validation output in $name" }
        Write-Output "$name : capture and GPU verification passed"
    }
    $demo = Get-Content "$output/wind-controls.stdout.log" -Raw
    if ($demo -notmatch 'strength 16.0, direction 90' -or $demo -notmatch 'strength 2.0, direction 180') {
        throw 'Wind key callbacks did not apply expected settings'
    }
} finally {
    foreach ($name in $names) { [Environment]::SetEnvironmentVariable($name, $saved[$name]) }
}
