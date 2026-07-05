$sourceFile = "app/src/main/res/mipmap-xxxhdpi/practice_trainning_launcher.webp"
$densities = @("mdpi", "hdpi", "xhdpi", "xxhdpi", "xxxhdpi")

Write-Host "Copying launcher icons..."

foreach ($density in $densities) {
    $targetDir = "app/src/main/res/mipmap-$density"
    $launcherFile = "$targetDir/ic_launcher.webp"
    $roundFile = "$targetDir/ic_launcher_round.webp"
    
    Write-Host "Copying to $density..."
    
    if (Test-Path $sourceFile) {
        Copy-Item -Path $sourceFile -Destination $launcherFile -Force
        Copy-Item -Path $sourceFile -Destination $roundFile -Force
        Write-Host "  Successfully copied to $density"
    } else {
        Write-Host "  Source file