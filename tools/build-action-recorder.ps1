$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path -Parent $PSScriptRoot
$taskCompiler = Join-Path $env:WINDIR 'Microsoft.NET/Framework64/v4.0.30319/csc.exe'
$taskOutput = Join-Path $taskRoot 'dist/AntiMicroX-RandomTurbo/ActionRecorder.exe'
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $taskOutput) | Out-Null
& $taskCompiler /nologo /target:winexe /platform:x64 /optimize+ /reference:System.Windows.Forms.dll /reference:System.Drawing.dll /reference:System.Web.Extensions.dll /reference:System.Xml.Linq.dll "/out:$taskOutput" (Join-Path $PSScriptRoot 'ActionRecorder.cs') (Join-Path $PSScriptRoot 'InputStudy.cs')
if ($LASTEXITCODE -ne 0) { throw 'Recorder compilation failed.' }
$taskTest = Start-Process -FilePath $taskOutput -ArgumentList '--self-test' -WindowStyle Hidden -PassThru -Wait
if ($taskTest.ExitCode -ne 0) { throw 'Recorder self-test failed.' }
$taskBuildBin = Join-Path $taskRoot 'build-random-turbo/bin'
if (Test-Path -LiteralPath $taskBuildBin) { Copy-Item -LiteralPath $taskOutput -Destination $taskBuildBin }
Write-Output 'Recorder built and offline tests passed.'
