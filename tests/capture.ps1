param([string]$Configuration = 'Debug')
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$output = Join-Path $root 'build/captures'
New-Item -ItemType Directory -Force $output | Out-Null
$cases = @(
    @{ Name='stage-static'; Arguments=@('--static','--no-lod','--culling','none','--view','overview','--orbit','--frames','180','--warmup','60','--capture-every','4') },
    @{ Name='stage-simulation'; Arguments=@('--no-lod','--culling','none','--view','overview','--orbit','--frames','180','--warmup','60','--capture-every','4') },
    @{ Name='stage-culling'; Arguments=@('--no-lod','--culling','all','--view','overview','--orbit','--frames','180','--warmup','60','--capture-every','4') },
    @{ Name='lod-fixed'; Arguments=@('--no-lod','--culling','none','--view','distant','--blades','65536','--frames','120','--warmup','119') },
    @{ Name='lod-distance'; Arguments=@('--culling','none','--view','distant','--blades','65536','--frames','120','--warmup','119') },
    @{ Name='wind'; Arguments=@('--view','close','--frames','180','--warmup','60','--capture-every','4') },
    @{ Name='orbit'; Arguments=@('--view','overview','--orbit','--frames','180','--warmup','60','--capture-every','4') },
    @{ Name='none'; Arguments=@('--view','close','--blades','65536','--culling','none','--frames','120','--warmup','119') },
    @{ Name='all'; Arguments=@('--view','close','--blades','65536','--culling','all','--frames','120','--warmup','119') },
    @{ Name='simulated'; Arguments=@('--view','overview','--frames','120','--warmup','119') }
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
    if ($run.ExitCode -ne 0) { throw 'Capture failed; see build/captures logs' }
    foreach ($name in (@($cases | ForEach-Object { $_.Name }) + @('presented'))) {
        if ((Get-Item "$output/$name.stderr.log").Length -ne 0) { throw "Validation error in $name" }
        $text = Get-Content "$output/$name.stdout.log" -Raw
        if ($text -match 'Validation (Warning|Error)|VUID-|SYNC-HAZARD') { throw "Validation output in $name" }
        Write-Output "$name : capture and GPU verification passed"
    }
} finally {
    foreach ($name in $names) { [Environment]::SetEnvironmentVariable($name, $saved[$name]) }
}
