param([int]$Samples = 180, [int]$Warmup = 60, [int]$Repeats = 2)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$output = Join-Path $root 'performance/lod'
New-Item -ItemType Directory -Force $output | Out-Null
$oldLayers = $env:VK_INSTANCE_LAYERS
$oldDisable = $env:VK_LOADER_LAYERS_DISABLE
$env:VK_LOADER_LAYERS_DISABLE = '~implicit~'
$env:VK_INSTANCE_LAYERS = $null
$jobs = @()
foreach ($repeat in 1..$Repeats) {
    foreach ($view in @('close','overview','distant')) {
        foreach ($lod in @('fixed','distance')) {
            $jobs += [pscustomobject]@{ Count=65536; Mode='none'; View=$view; Repeat=$repeat; Lod=$lod }
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
        $name = "$($job.View)-$($job.Count)-$($job.Lod)-$($job.Repeat)"
        $arguments = @('--frames',"$($Samples + $Warmup)",'--warmup',"$Warmup",'--blades',"$($job.Count)",
            '--culling',$job.Mode,'--view',$job.View,'--fixed-dt','0.016666667','--benchmark',"$output/$name.csv",'--offscreen','--no-occlusion')
        if ($job.Lod -eq 'fixed') { $arguments += '--no-lod' }
        $batchLines += (@("$output/$name.stdout.log", "$output/$name.stderr.log") + $arguments) -join "`t"
    }
    $batchPath = Join-Path $root 'build/lod-benchmarks.tsv'
    [IO.File]::WriteAllLines($batchPath, $batchLines, [Text.UTF8Encoding]::new($false))
    $run = Start-Process -FilePath (Join-Path $root 'bin/Release/vulkan_grass_rendering.exe') -ArgumentList '--batch', ('"' + $batchPath + '"') -WorkingDirectory (Join-Path $root 'build/src') -WindowStyle Hidden -PassThru -Wait
    if ($run.ExitCode -ne 0) { throw 'Benchmark batch failed; see measurement logs' }
    foreach ($job in $jobs) {
        $name = "$($job.View)-$($job.Count)-$($job.Lod)-$($job.Repeat)"
        if ((Get-Item "$output/$name.stderr.log").Length -ne 0) { throw "Benchmark failed: $name" }
        $text = Get-Content "$output/$name.stdout.log" -Raw
        if ($text -notmatch 'Final visible blades: (\d+)') { throw "Missing visible count: $name" }
        $summary += [pscustomobject]@{ file="$name.csv"; view=$job.View; blades=$job.Count; culling=$job.Mode; lod=$job.Lod; repeat=$job.Repeat; final_visible=[int]$Matches[1]; samples=$Samples; warmup=$Warmup }
        Write-Output "$name : $($Matches[1]) visible"
    }
    $summary | ConvertTo-Json | Set-Content (Join-Path $root 'performance/lod/runs.json')
} finally {
    $env:VK_INSTANCE_LAYERS = $oldLayers
    $env:VK_LOADER_LAYERS_DISABLE = $oldDisable
}
