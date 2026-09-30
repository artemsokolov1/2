param([string]$EngineRoot = 'F:\UnrealEngine\UE_5.8')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$editor = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$project = Join-Path $projectRoot 'MiniFootball.uproject'
Set-Item -Path Env:UE-LocalDataCachePath -Value (Join-Path $projectRoot 'DerivedDataCache')
$common = @('-ddc=InstalledNoZenLocalFallback', '-unattended', '-nullrhi', '-nosound', '-NoSplash')
& $editor $project @common '-run=pythonscript' "-script=$PSScriptRoot\import_dogs.py" "-abslog=$projectRoot\Saved\Logs\DogImportFinal.log"
if ($LASTEXITCODE -ne 0) { throw 'Dog import failed. See Saved/Logs/DogImportFinal.log.' }
& $editor $project @common '-ExecCmds=Automation RunTests MiniFootball.Dogs' '-TestExit=Automation Test Queue Empty' "-ReportExportPath=$projectRoot\Saved\TestReports\Dogs" "-abslog=$projectRoot\Saved\Logs\DogTests.log"
if ($LASTEXITCODE -ne 0) { throw 'Dog integration tests failed. See Saved/Logs/DogTests.log.' }
$report = Join-Path $projectRoot 'Saved\TestReports\Dogs\index.json'
if (-not (Test-Path -LiteralPath $report)) { throw 'Automation test report was not produced.' }
$results = Get-Content -LiteralPath $report -Raw | ConvertFrom-Json
if ($results.failed -gt 0 -or $results.succeeded -lt 1) { throw 'Dog integration test did not pass.' }
Write-Output 'DOG_VERIFICATION_OK'
