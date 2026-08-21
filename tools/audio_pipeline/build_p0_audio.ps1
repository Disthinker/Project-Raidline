param(
    [string]$ArtWorkbenchRoot = 'E:\WorkPlace\Projects\ArtWorkbench',
    [string]$OutputRoot = ''
)

$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = Join-Path $repoRoot 'assets\audio\v1'
}
$sourceRoot = Join-Path $ArtWorkbenchRoot '07_音效候选'
$ffmpeg = (Get-Command ffmpeg -ErrorAction Stop).Source
$temporaryRoot = Join-Path (
    [System.IO.Path]::GetTempPath()) (
    'raidline-audio-p0-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $temporaryRoot | Out-Null

Add-Type -AssemblyName System.IO.Compression.FileSystem

$sources = @(
    @('freeweaponsounds.zip', 'FreeWeaponSounds/AssaultRifle/Gunshots/assault_rifle_gunshot_01.wav', 'rifle_fire_01.wav'),
    @('freeweaponsounds.zip', 'FreeWeaponSounds/AssaultRifle/Gunshots/assault_rifle_gunshot_02.wav', 'rifle_fire_02.wav'),
    @('freeweaponsounds.zip', 'FreeWeaponSounds/AssaultRifle/Gunshots/assault_rifle_gunshot_03.wav', 'rifle_fire_03.wav'),
    @('freeweaponsounds.zip', 'FreeWeaponSounds/AssaultRifle/Gunshots/assault_rifle_tail_01.wav', 'rifle_tail.wav'),
    @('freeweaponsounds.zip', 'FreeWeaponSounds/AssaultRifle/Foley/01_assault_rifle_reload_1_drop_the_mag.wav', 'rifle_mag_out.wav'),
    @('freeweaponsounds.zip', 'FreeWeaponSounds/AssaultRifle/Foley/02_assault_rifle_reload_1_insert_the_mag.wav', 'rifle_mag_in.wav'),
    @('freeweaponsounds.zip', 'FreeWeaponSounds/AssaultRifle/Foley/03_assault_rifle_reload_1_bolt.wav', 'rifle_bolt.wav'),
    @('freeweaponsounds.zip', 'FreeWeaponSounds/AssaultRifle/Foley/assault_rifle_weapon_equip.wav', 'rifle_equip.wav'),
    @('freeweaponsounds.zip', 'FreeWeaponSounds/Handgun/Gunshots/handgun_gunshot_01.wav', 'pistol_fire_01.wav'),
    @('freeweaponsounds.zip', 'FreeWeaponSounds/Handgun/Gunshots/handgun_gunshot_02.wav', 'pistol_fire_02.wav'),
    @('freeweaponsounds.zip', 'FreeWeaponSounds/Handgun/Gunshots/handgun_gunshot_03.wav', 'pistol_fire_03.wav'),
    @('freeweaponsounds.zip', 'FreeWeaponSounds/Handgun/Gunshots/handgun_tail_01.wav', 'pistol_tail.wav'),
    @('freeweaponsounds.zip', 'FreeWeaponSounds/Handgun/Foley/02_drop_the_mag_handgun_reload_1.wav', 'pistol_mag_out.wav'),
    @('freeweaponsounds.zip', 'FreeWeaponSounds/Handgun/Foley/03_insert_the_mag_handgun_reload_1.wav', 'pistol_mag_in.wav'),
    @('freeweaponsounds.zip', 'FreeWeaponSounds/Handgun/Foley/04_slide_release_handgun_reload_1.wav', 'pistol_chamber.wav'),
    @('freeweaponsounds.zip', 'FreeWeaponSounds/Handgun/Foley/handgun_weapon_equip.wav', 'pistol_equip.wav'),
    @('Sonniss.com-GDC2026-GameAudioBundle1of5.zip', '344 Audio - Cinematic Fight Vol. 1/FGHTImpt_4 x Punch, Body 02_344 Audio_Cinematic Fight Vol 1.wav', 'body_impacts.wav'),
    @('Sonniss.com-GDC2026-GameAudioBundle2of5.zip', 'Epic Stock Media - HD Lock And Mechanism Sound Design Kit/MACHMech_Mechanism Counting Machine Interact Loose Container Short 01_ESM_HDLM.wav', 'mechanism_count.wav'),
    @('Sonniss.com-GDC2026-GameAudioBundle2of5.zip', 'Epic Stock Media - HD Lock And Mechanism Sound Design Kit/MECHLtch_Click Deep Mechanism Latch Button Nearfield Thunk 02_ESM_HDLM.wav', 'mechanism_latch.wav'),
    @('Sonniss.com-GDC2026-GameAudioBundle2of5.zip', 'Epic Stock Media - HD Lock And Mechanism Sound Design Kit/METLTonl_Item Spring Wire Impact Flick Top Clatter Light Tap Roll Handling Short 01_ESM_HDLM.wav', 'mechanism_spring.wav'),
    @('Sonniss.com-GDC2026-GameAudioBundle2of5.zip', 'Epic Stock Media - Humanoid Creatures Vol 4 - Monstrous and Undead Creature Vocalization Sound Sets/HMNBrth_Construction Kit Male Screeching Breath Inhale Weak Squeal 05_ESM_HC4.wav', 'infected_alert.wav'),
    @('Sonniss.com-GDC2026-GameAudioBundle2of5.zip', 'Epic Stock Media - Humanoid Creatures Vol 4 - Monstrous and Undead Creature Vocalization Sound Sets/VOXReac_Construction Kit Male Flutter Death Vocal Stuttered Long 05_ESM_HC4.wav', 'infected_death.wav'),
    @('Sonniss.com-GDC2026-GameAudioBundle3of5.zip', 'InMotionAudio - Foley T-Shirt/FOLYClth_ClothMovement24_InMotionAudio_FoleyT-Shirt.wav', 'cloth_24.wav'),
    @('Sonniss.com-GDC2026-GameAudioBundle3of5.zip', 'InMotionAudio - Foley T-Shirt/FOLYClth_ClothMovement29_InMotionAudio_FoleyT-Shirt.wav', 'cloth_29.wav'),
    @('Sonniss.com-GDC2026-GameAudioBundle3of5.zip', 'InMotionAudio - Foley T-Shirt/FOLYClth_SinglePats04_InMotionAudio_FoleyT-Shirt.wav', 'cloth_pat.wav'),
    @('Sonniss.com-GDC2026-GameAudioBundle3of5.zip', 'InMotionAudio - Instrument Case/OBJLug_Case_Closed06_InMotionAudio_InstrumentCase.wav', 'case_close.wav'),
    @('Sonniss.com-GDC2026-GameAudioBundle3of5.zip', 'InMotionAudio - Instrument Case/OBJLug_CaseDown_Concrete12_InMotionAudio_InstrumentCase.wav', 'case_down.wav'),
    @('Sonniss.com-GDC2026-GameAudioBundle3of5.zip', 'InMotionAudio - Medical Thermometer/BEEPMed_Thermometer_Beep11_InMotionAudio_MedicalThermometer.wav', 'medical_beep.wav'),
    @('Sonniss.com-GDC2026-GameAudioBundle3of5.zip', 'InMotionAudio - Medical Thermometer/MACHMed_Thermometer_ButtonPress_Beep03_InMotionAudio_MedicalThermometer.wav', 'medical_press.wav'),
    @('Sonniss.com-GDC2026-GameAudioBundle3of5.zip', 'InMotionAudio - Chimney Wind/WINDInt_ChimneyWind05_InMotionAudio_ChimneyWind.wav', 'base_chimney_wind.wav'),
    @('Sonniss.com-GDC2026-GameAudioBundle5of5.zip', 'SoundBits - Vox Bestiae - Source Elements/CREAHmn_Violent Humanoid Creature Exhale Short 4_SNDBTS_VB-SE.wav', 'infected_hit.wav'),
    @('Sonniss.com-GDC2026-GameAudioBundle5of5.zip', 'The Noisery - City Rain/WINDInt_Wind Strong Metal Rattle_The Noisery_City Rain.wav', 'raid_wind_metal.wav')
)

function Invoke-CheckedFfmpeg([string[]]$Arguments) {
    & $ffmpeg @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "ffmpeg failed with exit code $LASTEXITCODE"
    }
}

function Convert-Clip(
    [string]$Source,
    [string]$Destination,
    [double]$Start,
    [double]$Duration,
    [double]$Loudness,
    [string]$ExtraFilter = '') {
    $directory = Split-Path -Parent $Destination
    New-Item -ItemType Directory -Path $directory -Force | Out-Null
    $filters = @('aresample=48000')
    if (-not [string]::IsNullOrWhiteSpace($ExtraFilter)) {
        $filters += $ExtraFilter
    }
    $filters += 'aformat=sample_fmts=fltp:channel_layouts=mono'
    $filters += "loudnorm=I=$Loudness`:TP=-1.5:LRA=7"
    $arguments = @('-y', '-hide_banner', '-loglevel', 'error')
    if ($Start -gt 0.0) {
        $arguments += @('-ss', $Start.ToString([Globalization.CultureInfo]::InvariantCulture))
    }
    $arguments += @('-i', $Source)
    if ($Duration -gt 0.0) {
        $arguments += @('-t', $Duration.ToString([Globalization.CultureInfo]::InvariantCulture))
    }
    $arguments += @(
        '-af', ($filters -join ','),
        '-ar', '48000', '-ac', '1', '-c:a', 'pcm_s16le',
        $Destination)
    Invoke-CheckedFfmpeg $arguments
}

function Convert-Loop(
    [string]$Source,
    [string]$Destination,
    [double]$Start,
    [double]$Loudness,
    [string]$ExtraFilter = '') {
    $directory = Split-Path -Parent $Destination
    New-Item -ItemType Directory -Path $directory -Force | Out-Null
    $end = $Start + 22.0
    $preparation = 'aresample=48000,aformat=sample_fmts=fltp:channel_layouts=mono'
    if (-not [string]::IsNullOrWhiteSpace($ExtraFilter)) {
        $preparation += ",$ExtraFilter"
    }
    $preparation += ",loudnorm=I=$Loudness`:TP=-3:LRA=4"
    $filter = "[0:a]atrim=start=$Start`:end=$end,asetpts=PTS-STARTPTS,$preparation,asplit=3[whole][headsrc][tailsrc];[whole]atrim=start=2:end=20,asetpts=PTS-STARTPTS[body];[headsrc]atrim=start=0:end=2,asetpts=PTS-STARTPTS[head];[tailsrc]atrim=start=20:end=22,asetpts=PTS-STARTPTS[tail];[tail][head]acrossfade=d=2:c1=tri:c2=tri[cross];[body][cross]concat=n=2:v=0:a=1[out]"
    Invoke-CheckedFfmpeg @(
        '-y', '-hide_banner', '-loglevel', 'error', '-i', $Source,
        '-filter_complex', $filter, '-map', '[out]',
        '-ar', '48000', '-ac', '1', '-c:a', 'pcm_s16le',
        $Destination)
}

try {
    foreach ($group in ($sources | Group-Object { $_[0] })) {
        $archivePath = Join-Path $sourceRoot $group.Name
        if (-not (Test-Path -LiteralPath $archivePath -PathType Leaf)) {
            throw "Missing source archive: $archivePath"
        }
        $archive = [System.IO.Compression.ZipFile]::OpenRead($archivePath)
        try {
            foreach ($source in $group.Group) {
                $entry = $archive.GetEntry($source[1])
                if ($null -eq $entry) {
                    throw "Missing archive entry: $($source[1])"
                }
                [System.IO.Compression.ZipFileExtensions]::ExtractToFile(
                    $entry,
                    (Join-Path $temporaryRoot $source[2]),
                    $false)
            }
        }
        finally {
            $archive.Dispose()
        }
    }

    $t = $temporaryRoot
    $o = $OutputRoot
    foreach ($index in 1..3) {
        Convert-Clip (Join-Path $t "pistol_fire_0$index.wav") (Join-Path $o "weapon\pistol_fire_0$index.wav") 0 1.25 -14
        Convert-Clip (Join-Path $t "rifle_fire_0$index.wav") (Join-Path $o "weapon\rifle_fire_0$index.wav") 0 1.50 -14
    }
    Convert-Clip (Join-Path $t 'pistol_tail.wav') (Join-Path $o 'weapon\outdoor_tail_01.wav') 0 2.0 -24 'afade=t=out:st=1.65:d=0.35'
    Convert-Clip (Join-Path $t 'rifle_tail.wav') (Join-Path $o 'weapon\outdoor_tail_02.wav') 0 2.0 -24 'afade=t=out:st=1.65:d=0.35'
    Convert-Clip (Join-Path $t 'pistol_mag_out.wav') (Join-Path $o 'weapon\mag_out_01.wav') 0 0 -18
    Convert-Clip (Join-Path $t 'rifle_mag_out.wav') (Join-Path $o 'weapon\mag_out_02.wav') 0 0 -18
    Convert-Clip (Join-Path $t 'pistol_mag_in.wav') (Join-Path $o 'weapon\mag_in_01.wav') 0 0 -18
    Convert-Clip (Join-Path $t 'rifle_mag_in.wav') (Join-Path $o 'weapon\mag_in_02.wav') 0 0 -18
    Convert-Clip (Join-Path $t 'pistol_chamber.wav') (Join-Path $o 'weapon\chamber_01.wav') 0 0 -17
    Convert-Clip (Join-Path $t 'rifle_bolt.wav') (Join-Path $o 'weapon\chamber_02.wav') 0 0 -17
    Convert-Clip (Join-Path $t 'mechanism_latch.wav') (Join-Path $o 'weapon\dry_fire_01.wav') 0 0 -20
    Convert-Clip (Join-Path $t 'pistol_chamber.wav') (Join-Path $o 'weapon\malfunction_clear_01.wav') 0 0 -18 'asetrate=45600,aresample=48000'
    Convert-Clip (Join-Path $t 'rifle_bolt.wav') (Join-Path $o 'weapon\malfunction_clear_02.wav') 0 0 -18 'asetrate=45600,aresample=48000'

    Convert-Clip (Join-Path $t 'mechanism_count.wav') (Join-Path $o 'ui\confirm_01.wav') 0 0 -22
    Convert-Clip (Join-Path $t 'mechanism_latch.wav') (Join-Path $o 'ui\deny_01.wav') 0 0 -22 'asetrate=43200,aresample=48000'
    Convert-Clip (Join-Path $t 'cloth_24.wav') (Join-Path $o 'inventory\pickup_01.wav') 0 0.55 -22
    Convert-Clip (Join-Path $t 'cloth_29.wav') (Join-Path $o 'inventory\pickup_02.wav') 0 0.55 -22
    Convert-Clip (Join-Path $t 'pistol_equip.wav') (Join-Path $o 'inventory\equip_01.wav') 0 0 -20
    Convert-Clip (Join-Path $t 'rifle_equip.wav') (Join-Path $o 'inventory\equip_02.wav') 0 0 -20
    Convert-Clip (Join-Path $t 'case_close.wav') (Join-Path $o 'inventory\place_01.wav') 0 0 -22
    Convert-Clip (Join-Path $t 'case_down.wav') (Join-Path $o 'inventory\place_02.wav') 0 0 -22

    Convert-Clip (Join-Path $t 'cloth_pat.wav') (Join-Path $o 'medical\start_01.wav') 0 0 -21
    Convert-Clip (Join-Path $t 'medical_beep.wav') (Join-Path $o 'medical\complete_01.wav') 0 0 -22
    Convert-Clip (Join-Path $t 'medical_press.wav') (Join-Path $o 'medical\interrupt_01.wav') 0 0.25 -23 'asetrate=43200,aresample=48000'

    $impactStarts = @(0.13, 1.10, 2.15, 3.15)
    foreach ($index in 0..3) {
        $number = $index + 1
        Convert-Clip (Join-Path $t 'body_impacts.wav') (Join-Path $o "character\hurt_0$number.wav") $impactStarts[$index] 0.48 -18
    }
    Convert-Clip (Join-Path $t 'infected_alert.wav') (Join-Path $o 'infected\alert_01.wav') 0 0 -20
    Convert-Clip (Join-Path $t 'infected_alert.wav') (Join-Path $o 'infected\alert_02.wav') 0 0 -20 'asetrate=50400,aresample=48000'
    Convert-Clip (Join-Path $t 'infected_hit.wav') (Join-Path $o 'infected\hit_01.wav') 0 0 -20
    Convert-Clip (Join-Path $t 'infected_hit.wav') (Join-Path $o 'infected\hit_02.wav') 0 0 -20 'asetrate=45600,aresample=48000'
    Convert-Clip (Join-Path $t 'infected_death.wav') (Join-Path $o 'infected\death_01.wav') 0 2.6 -21
    Convert-Clip (Join-Path $t 'infected_death.wav') (Join-Path $o 'infected\death_02.wav') 0 2.6 -21 'asetrate=44160,aresample=48000'

    Convert-Clip (Join-Path $t 'body_impacts.wav') (Join-Path $o 'impact\enemy_01.wav') 0.13 0.48 -20
    Convert-Clip (Join-Path $t 'mechanism_spring.wav') (Join-Path $o 'impact\obstacle_01.wav') 0.26 0.70 -22
    Convert-Clip (Join-Path $t 'case_down.wav') (Join-Path $o 'impact\ground_01.wav') 0 0.65 -24

    Convert-Loop (Join-Path $t 'base_chimney_wind.wav') (Join-Path $o 'ambience\base_safe_low.wav') 30 -38 'highpass=f=90,lowpass=f=3200'
    Convert-Loop (Join-Path $t 'raid_wind_metal.wav') (Join-Path $o 'ambience\raid_urban_low.wav') 10 -30
}
finally {
    $resolvedTemp = [System.IO.Path]::GetFullPath($temporaryRoot)
    $systemTemp = [System.IO.Path]::GetFullPath([System.IO.Path]::GetTempPath())
    if ($resolvedTemp.StartsWith($systemTemp, [StringComparison]::OrdinalIgnoreCase) -and
        (Split-Path -Leaf $resolvedTemp).StartsWith('raidline-audio-p0-')) {
        Remove-Item -LiteralPath $resolvedTemp -Recurse -Force
    }
}

Get-ChildItem -LiteralPath $OutputRoot -Recurse -File |
    Sort-Object FullName |
    Select-Object FullName, Length
