param([ValidateSet('sponza-max', 'sponza-final', 'sponza-shadow-fixed', 'sponza-loading-fixed')][string]$Prefix = 'sponza-shadow-fixed')
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$earlySuffix = if ($Prefix -eq 'sponza-loading-fixed') { '-startup.png' } else { '-early.png' }
$early = [System.Drawing.Bitmap]::new((Join-Path $PSScriptRoot ($Prefix + $earlySuffix)))
$late = [System.Drawing.Bitmap]::new((Join-Path $PSScriptRoot ($Prefix + '-converged.png')))
$regions = @(
    @{Name='whole_image';X=0;Y=0;W=1920;H=1080;Step=4},
    @{Name='shaded_floor';X=760;Y=930;W=180;H=70;Step=1},
    @{Name='left_shaded_column';X=360;Y=700;W=70;H=140;Step=1},
    @{Name='sunlit_floor';X=880;Y=825;W=200;H=25;Step=1},
    @{Name='left_wall';X=500;Y=760;W=100;H=60;Step=1}
)
try {
    if ($early.Width -ne 1920 -or $early.Height -ne 1080 -or
        $late.Width -ne 1920 -or $late.Height -ne 1080) {
        throw 'Comparison regions require matching 1920x1080 Camera A captures.'
    }
    $measurements = foreach ($region in $regions) {
        [double]$difference = 0
        [double]$earlyLuma = 0
        [double]$lateLuma = 0
        [int]$count = 0
        for ($y = $region.Y; $y -lt ($region.Y + $region.H); $y += $region.Step) {
            for ($x = $region.X; $x -lt ($region.X + $region.W); $x += $region.Step) {
                $p = $early.GetPixel($x, $y)
                $q = $late.GetPixel($x, $y)
                $difference += [Math]::Abs([int]$p.R - [int]$q.R) +
                    [Math]::Abs([int]$p.G - [int]$q.G) + [Math]::Abs([int]$p.B - [int]$q.B)
                $earlyLuma += .2126 * $p.R + .7152 * $p.G + .0722 * $p.B
                $lateLuma += .2126 * $q.R + .7152 * $q.G + .0722 * $q.B
                $count++
            }
        }
        [pscustomobject]@{
            Region = $region.Name
            Rectangle = @($region.X, $region.Y, $region.W, $region.H)
            SampleStep = $region.Step
            Samples = $count
            MeanAbsoluteRgbDifference255 = $difference / (3 * $count)
            EarlyLuma255 = $earlyLuma / $count
            LateLuma255 = $lateLuma / $count
            LumaChangePercent = 100 * ($lateLuma / [Math]::Max($earlyLuma, .000001) - 1)
        }
    }
    $measurements | ConvertTo-Json -Depth 4 |
        Set-Content -LiteralPath (Join-Path $PSScriptRoot ($Prefix + '-stationary-metrics.json'))
    $measurements | Select-Object Region, MeanAbsoluteRgbDifference255, LumaChangePercent |
        Format-Table -AutoSize
} finally {
    $early.Dispose()
    $late.Dispose()
}
