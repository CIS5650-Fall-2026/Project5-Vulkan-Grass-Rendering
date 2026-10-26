param([string]$Configuration = 'Debug')
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$output = Join-Path $root 'build/interaction'
New-Item -ItemType Directory -Force $output | Out-Null
$cases = @(
    @{ Name='visible'; Arguments=@('--no-occlusion','--view','close','--culling','none','--blades','65536','--frames','120','--warmup','119') },
    @{ Name='occluded'; Arguments=@('--view','close','--culling','none','--blades','65536','--frames','120','--warmup','119') },
    @{ Name='no-contact'; Arguments=@('--no-collision','--no-occlusion','--view','close','--culling','none','--blades','65536','--frames','120','--warmup','119') },
    @{ Name='moving'; Arguments=@('--collision-demo','--view','overview','--frames','240','--warmup','0','--capture-every','8') },
    @{ Name='overview-visible'; Arguments=@('--no-occlusion','--view','overview','--culling','none','--blades','65536','--frames','120','--warmup','119') },
    @{ Name='overview-occluded'; Arguments=@('--view','overview','--culling','none','--blades','65536','--frames','120','--warmup','119') },
    @{ Name='distant-visible'; Arguments=@('--no-occlusion','--view','distant','--culling','none','--blades','65536','--frames','120','--warmup','119') },
    @{ Name='distant-occluded'; Arguments=@('--view','distant','--culling','none','--blades','65536','--frames','120','--warmup','119') },
    @{ Name='moving-reference'; Arguments=@('--collision-demo','--no-occlusion','--view','overview','--frames','240','--warmup','0','--capture-every','8') },
    @{ Name='overview-repeat'; Arguments=@('--no-occlusion','--view','overview','--culling','none','--blades','65536','--frames','120','--warmup','119') },
    @{ Name='distant-repeat'; Arguments=@('--no-occlusion','--view','distant','--culling','none','--blades','65536','--frames','120','--warmup','119') },
    @{ Name='moving-repeat'; Arguments=@('--collision-demo','--no-occlusion','--view','overview','--frames','240','--warmup','0','--capture-every','8') },
    @{ Name='away'; Arguments=@('--view','away','--culling','none','--frames','120','--warmup','119') }
)
$lines = @()
foreach ($case in $cases) {
    $directory = Join-Path $output $case.Name
    New-Item -ItemType Directory -Force $directory | Out-Null
    $arguments = @('--capture-dir',$directory,'--fixed-dt','0.016666667','--verify','--dump-blades',"$output/$($case.Name).bin") + $case.Arguments
    $lines += (@("$output/$($case.Name).stdout.log","$output/$($case.Name).stderr.log") + $arguments) -join "`t"
}
# Exercise the ordinary swapchain too, after the offscreen captures.
$lines += (@("$output/presented.stdout.log","$output/presented.stderr.log",'--frames','3','--verify','--dump-blades',"$output/$($case.Name).bin") -join "`t")
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
    if ((Get-FileHash "$output/visible.bin").Hash -ne (Get-FileHash "$output/occluded.bin").Hash) {
        throw 'Occlusion changed simulation'
    }
    if ((Get-FileHash "$output/visible.bin").Hash -eq (Get-FileHash "$output/no-contact.bin").Hash) {
        throw 'Contact did not change the grass'
    }
} finally {
    foreach ($name in $names) { [Environment]::SetEnvironmentVariable($name, $saved[$name]) }
}
