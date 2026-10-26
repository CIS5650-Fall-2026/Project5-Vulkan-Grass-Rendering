param([string]$Configuration = 'Debug')
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$exe = Join-Path $root "bin/$Configuration/vulkan_grass_rendering.exe"
$output = Join-Path $root 'build/validation'
New-Item -ItemType Directory -Force $output | Out-Null
$sdk = if ($env:VULKAN_SDK) { $env:VULKAN_SDK } else { Join-Path $root 'build/tools/VulkanSDK' }
$oldLayerPath = $env:VK_LAYER_PATH
$oldEnables = $env:VK_LAYER_ENABLES
$oldSync = $env:VK_LAYER_VALIDATE_SYNC
$oldLayers = $env:VK_INSTANCE_LAYERS
$oldDisable = $env:VK_LOADER_LAYERS_DISABLE
$env:VK_LOADER_LAYERS_DISABLE = '~implicit~'
$env:VK_LAYER_PATH = Join-Path $sdk 'Bin'
$env:VK_LAYER_ENABLES = $null
$env:VK_LAYER_VALIDATE_SYNC = '1'
$env:VK_INSTANCE_LAYERS = 'VK_LAYER_KHRONOS_validation'
$results = @()
$cases = @(
    @{ Name='none'; Count=8193; Mode='none'; View='overview'; Visible=8193 },
    @{ Name='all'; Count=8193; Mode='all'; View='overview' },
    @{ Name='orientation'; Count=8193; Mode='orientation'; View='overview' },
    @{ Name='frustum'; Count=8193; Mode='frustum'; View='close' },
    @{ Name='distance'; Count=8193; Mode='distance'; View='overview' },
    @{ Name='away'; Count=8193; Mode='frustum'; View='away'; Visible=0 },
    @{ Name='distant'; Count=8193; Mode='distance'; View='distant'; Visible=0 },
    @{ Name='single'; Count=1; Mode='none'; View='overview'; Visible=1 },
    @{ Name='static'; Count=8193; Mode='none'; View='overview'; Visible=8193; Extra=@('--static') },
    @{ Name='strong-wind'; Count=8193; Mode='all'; View='overview'; Extra=@('--wind','100') },
    @{ Name='long-run'; Count=8193; Mode='all'; View='overview'; Frames=600 },
    @{ Name='many'; Count=65537; Mode='all'; View='overview' },
    @{ Name='profile'; Count=8193; Mode='all'; View='overview'; Extra=@('--benchmark',"$output/profile.csv",'--warmup','30') }
)
try {
    $batchLines = @()
    foreach ($case in $cases) {
        $frames = if ($case.Frames) { $case.Frames } else { 120 }
        $arguments = @('--frames',"$frames",'--blades',"$($case.Count)",'--culling',$case.Mode,
            '--view',$case.View,'--fixed-dt','0.016666667','--verify','--dump-blades',"$output/$($case.Name).bin",'--offscreen','--no-collider')
        if ($case.Extra) { $arguments += $case.Extra }
        $batchLines += (@("$output/$($case.Name).stdout.log", "$output/$($case.Name).stderr.log") + $arguments) -join "`t"
    }
    $batchPath = "$output/cases.tsv"
    [IO.File]::WriteAllLines($batchPath, $batchLines, [Text.UTF8Encoding]::new($false))
    $run = Start-Process -FilePath $exe -ArgumentList '--batch', ('"' + $batchPath + '"') -WorkingDirectory (Join-Path $root 'build/src') -WindowStyle Hidden -PassThru -Wait
    if ($run.ExitCode -ne 0) { throw "GPU batch failed; see logs in $output" }
    foreach ($case in $cases) {
        $frames = if ($case.Frames) { $case.Frames } else { 120 }
        $stdout = "$output/$($case.Name).stdout.log"
        $stderr = "$output/$($case.Name).stderr.log"

        $text = Get-Content $stdout -Raw
        if ($text -match 'Validation (Warning|Error)|VUID-|SYNC-HAZARD') { throw "$($case.Name) validation output: $text" }
        if ($run.ExitCode -ne 0) { throw "$($case.Name) exited $($run.ExitCode): $(Get-Content $stderr -Raw)" }
        if ((Get-Item $stderr).Length -ne 0) { throw "$($case.Name) validation messages: $(Get-Content $stderr -Raw)" }
        if ($text -notmatch 'Verified (\d+) blades, (\d+) visible, (\d+) bent') { throw "Missing GPU verification for $($case.Name)" }
        $visible = [int]$Matches[2]
        $bent = [int]$Matches[3]
        if ($case.ContainsKey('Visible') -and $visible -ne $case.Visible) { throw "Unexpected visibility for $($case.Name): $visible" }
        if ($case.Name -eq 'static' -and $bent -ne 0) { throw 'Static blades moved' }
        if ($case.Name -ne 'static' -and $bent -eq 0) { throw "Simulation did not move $($case.Name)" }
        if ($case.Name -in @('all','orientation','frustum','distance','many') -and ($visible -eq 0 -or $visible -ge $case.Count)) { throw "Culling did not leave a partial field for $($case.Name)" }
        $results += [pscustomobject]@{ case=$case.Name; blades=$case.Count; visible=$visible; bent=$bent; frames=$frames; validation_messages=0 }
        Write-Output "$($case.Name): $visible / $($case.Count) visible, $bent bent; validation clean"
    }
    $reference = (Get-FileHash "$output/none.bin" -Algorithm SHA256).Hash
    foreach ($name in @('all','orientation','frustum','distance','away','distant','profile')) {
        if ((Get-FileHash "$output/$name.bin" -Algorithm SHA256).Hash -ne $reference) { throw "Culling or the camera changed simulation state: $name" }
    }
    $samples = Import-Csv "$output/profile.csv"
    if ($samples.Count -ne 90) { throw 'Wrong benchmark sample count' }
    foreach ($sample in $samples) {
        if ([double]$sample.compute_ms -le 0 -or [double]$sample.graphics_ms -le 0) { throw 'Invalid GPU timestamp result' }
    }
    $results | ConvertTo-Json | Set-Content (Join-Path $root 'performance/validation.json')
    Write-Output 'All GPU checks passed; simulation buffers match across culling modes and camera views.'
} finally {
    $env:VK_LAYER_PATH = $oldLayerPath
    $env:VK_LAYER_ENABLES = $oldEnables
    $env:VK_LAYER_VALIDATE_SYNC = $oldSync
    $env:VK_INSTANCE_LAYERS = $oldLayers
    $env:VK_LOADER_LAYERS_DISABLE = $oldDisable
}
