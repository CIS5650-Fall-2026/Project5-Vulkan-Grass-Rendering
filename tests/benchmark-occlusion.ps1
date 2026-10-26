param([int]$Samples = 180, [int]$Warmup = 60, [int]$Repeats = 2)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$output = Join-Path $root 'performance/occlusion'
New-Item -ItemType Directory -Force $output | Out-Null
$oldLayers = $env:VK_INSTANCE_LAYERS
$oldDisable = $env:VK_LOADER_LAYERS_DISABLE
$env:VK_LOADER_LAYERS_DISABLE = '~implicit~'
$env:VK_INSTANCE_LAYERS = $null
$jobs = @()
foreach ($repeat in 1..$Repeats) {
    foreach ($view in @('close','overview','distant')) {
        foreach ($occlusion in @('off','on')) {
            $jobs += [pscustomobject]@{ Count=65536; Mode='none'; View=$view; Repeat=$repeat; Occlusion=$occlusion }
        }
    }
}
# Fixed shuffled order reduces systematic warmup and temperature bias.
$random = [Random]::new(565)
$jobs = $jobs | Sort-Object { $random.Next() }
$summary = @()
try {
    $batchLines = @()
    foreach ($job in $jobs) {
        $name = "$($job.View)-$($job.Count)-$($job.Occlusion)-$($job.Repeat)"
        $arguments = @('--frames',"$($Samples + $Warmup)",'--warmup',"$Warmup",'--blades',"$($job.Count)",
            '--culling',$job.Mode,'--view',$job.View,'--fixed-dt','0.016666667','--benchmark',"$output/$name.csv",'--offscreen')
        if ($job.Occlusion -eq 'off') { $arguments += '--no-occlusion' }
        $batchLines += (@("$output/$name.stdout.log", "$output/$name.stderr.log") + $arguments) -join "`t"
    }
    $batchPath = Join-Path $root 'build/occlusion-benchmarks.tsv'
    [IO.File]::WriteAllLines($batchPath, $batchLines, [Text.UTF8Encoding]::new($false))
    $run = Start-Process -FilePath (Join-Path $root 'bin/Release/vulkan_grass_rendering.exe') -ArgumentList '--batch', ('"' + $batchPath + '"') -WorkingDirectory (Join-Path $root 'build/src') -WindowStyle Hidden -PassThru -Wait
    if ($run.ExitCode -ne 0) { throw 'Benchmark batch failed; see measurement logs' }
    foreach ($job in $jobs) {
        $name = "$($job.View)-$($job.Count)-$($job.Occlusion)-$($job.Repeat)"
        if ((Get-Item "$output/$name.stderr.log").Length -ne 0) { throw "Benchmark failed: $name" }
        $text = Get-Content "$output/$name.stdout.log" -Raw
        if ($text -notmatch 'Final visible blades: (\d+)') { throw "Missing visible count: $name" }
        $summary += [pscustomobject]@{ file="$name.csv"; view=$job.View; blades=$job.Count; culling=$job.Mode; occlusion=$job.Occlusion; repeat=$job.Repeat; final_visible=[int]$Matches[1]; samples=$Samples; warmup=$Warmup }
        Write-Output "$name : $($Matches[1]) visible"
    }
    $summary | ConvertTo-Json | Set-Content (Join-Path $root 'performance/occlusion/runs.json')
} finally {
    $env:VK_INSTANCE_LAYERS = $oldLayers
    $env:VK_LOADER_LAYERS_DISABLE = $oldDisable
}
