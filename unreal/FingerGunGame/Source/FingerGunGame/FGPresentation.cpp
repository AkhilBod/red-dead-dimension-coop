#include "FGPresentation.h"

#include "Animation/AnimSequence.h"
#include "Components/AudioComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/SkyLight.h"
#include "EngineUtils.h"
#include "FGAssets.h"
#include "FGFx.h"
#include "FGGameState.h"
#include "FGTrain.h"
#include "FGWorldStreamer.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Sound/SoundBase.h"
#include "UnrealClient.h"

AFGPresentation::AFGPresentation()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickGroup = TG_PrePhysics;
    bReplicates = false;
    SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
}

AFGPresentation* AFGPresentation::Get(const UObject* WorldContext)
{
    UWorld* W = WorldContext ? WorldContext->GetWorld() : nullptr;
    if (!W) { return nullptr; }
    for (TActorIterator<AFGPresentation> It(W); It; ++It)
    {
        if (IsValid(*It)) { return *It; }
    }
    FActorSpawnParameters Params;
    Params.ObjectFlags |= RF_Transient;
    AFGPresentation* P = W->SpawnActor<AFGPresentation>(Params);
    if (P) { P->Build(); }
    return P;
}

void AFGPresentation::Build()
{
    UWorld* W = GetWorld();
    bAuthority = GetNetMode() != NM_Client;
    BuildSky();
    World = W->SpawnActor<AFGWorldStreamer>();
    World->bReplica = !bAuthority;
    if (bAuthority)
    {
        // The host writes down every chunk and signal arm it lays, for clients to lay the same.
        World->OnChunkAppended = [this](const FFGChunkRec& Rec)
        {
            if (AFGGameState* GS = GetWorld()->GetGameState<AFGGameState>()) { GS->PushChunk(Rec); }
        };
        World->OnLeanAdded = [this](int32 Id, double AtS, float Side)
        {
            if (AFGGameState* GS = GetWorld()->GetGameState<AFGGameState>()) { GS->PushLean(Id, AtS, Side); }
        };
    }
    BuildTrains();
    bPerf = FParse::Param(FCommandLine::Get(), TEXT("FGPerf"));
    FParse::Value(FCommandLine::Get(), TEXT("FGShots="), ShotEvery);

    if (USoundBase* Loop = FGAssets::Sound(TEXT("train_loop")))
    {
        TrainLoop = UGameplayStatics::SpawnSound2D(this, Loop, 0.0f, 1.0f, 0.0f, nullptr, false, false);
    }
    // Played with it on: the team preferred the train and the guns on their own. -FGMusic brings the loops back.
    if (FParse::Param(FCommandLine::Get(), TEXT("FGMusic")))
    {
        const TCHAR* Tracks[3] = { TEXT("music_day"), TEXT("music_night"), TEXT("music_boss") };
        for (int32 i = 0; i < 3; ++i)
        {
            if (USoundBase* Track = FGAssets::Sound(Tracks[i]))
            {
                Music[i] = UGameplayStatics::SpawnSound2D(this, Track, 0.001f, 1.0f, 0.0f, nullptr, false, false);
            }
        }
    }
}

void AFGPresentation::BuildSky()
{
    UWorld* W = GetWorld();
    if (TActorIterator<ADirectionalLight> It(W); It)
    {
        Sun = *It;      // the level brought its own lighting: leave it alone
        SunIntensity = Sun->GetLightComponent()->Intensity;
        for (TActorIterator<ASkyLight> SkyIt(W); SkyIt; ++SkyIt) { Sky = *SkyIt; }
        return;
    }
    // Golden hour: low warm sun ahead and to the left, long shadows, haze.
    bOwnSky = true;
    Sun = W->SpawnActor<ADirectionalLight>(FVector(0, 0, 3000), FRotator(-16.0f, 150.0f, 0.0f));
    UDirectionalLightComponent* SunComp = Cast<UDirectionalLightComponent>(Sun->GetLightComponent());
    SunComp->SetMobility(EComponentMobility::Movable);
    SunComp->SetIntensity(SunIntensity);
    SunComp->SetLightColor(FLinearColor(1.0f, 0.80f, 0.58f));
    SunComp->SetAtmosphereSunLight(true);
    SunComp->DynamicShadowDistanceMovableLight = 12000.0f;
    SunComp->DynamicShadowCascades = 2;

    W->SpawnActor<AActor>(ASkyAtmosphere::StaticClass(), FTransform::Identity);

    Sky = W->SpawnActor<ASkyLight>();
    Sky->GetLightComponent()->SetMobility(EComponentMobility::Movable);
    Sky->GetLightComponent()->SetRealTimeCapture(false);        // re-capturing the sky every frame cost several fps; it never changes
    Sky->GetLightComponent()->SetIntensity(1.3f);

    Fog = W->SpawnActor<AExponentialHeightFog>();
    UExponentialHeightFogComponent* FogComp = Fog->GetComponent();
    FogComp->SetFogDensity(0.018f);
    FogComp->SetFogHeightFalloff(0.08f);
    FogComp->SetFogInscatteringColor(FLinearColor(0.85f, 0.55f, 0.32f));
    FogComp->SetStartDistance(3000.0f);

    APostProcessVolume* PP = W->SpawnActor<APostProcessVolume>();
    PP->bUnbound = true;
    FPostProcessSettings& S = PP->Settings;
    S.bOverride_VignetteIntensity = true;       S.VignetteIntensity = 0.65f;
    S.bOverride_FilmGrainIntensity = true;      S.FilmGrainIntensity = 0.35f;
    S.bOverride_WhiteTemp = true;               S.WhiteTemp = 7300.0f;
    S.bOverride_ColorSaturation = true;         S.ColorSaturation = FVector4(0.92f, 0.90f, 0.86f, 1.0f);
    S.bOverride_ColorContrast = true;           S.ColorContrast = FVector4(1.08f, 1.08f, 1.08f, 1.0f);
    S.bOverride_BloomIntensity = true;          S.BloomIntensity = 0.9f;
    S.bOverride_MotionBlurAmount = true;        S.MotionBlurAmount = 0.35f;
    S.bOverride_AutoExposureMinBrightness = true; S.AutoExposureMinBrightness = 0.6f;
    S.bOverride_AutoExposureMaxBrightness = true; S.AutoExposureMaxBrightness = 1.6f;
}

void AFGPresentation::BuildTrains()
{
    UWorld* W = GetWorld();
    Train = W->SpawnActor<AFGTrain>();
    Train->Build({ TEXT("Locomotive"), TEXT("Tender"), TEXT("Boxcar"), TEXT("PassengerCar"), TEXT("Flatcar_Crates"), TEXT("Caboose") }, 3, false);
    Train->Place(World, 0.0f);

    BanditTrain = W->SpawnActor<AFGTrain>();
    BanditTrain->Build({ TEXT("Locomotive"), TEXT("Tender"), TEXT("Boxcar"), TEXT("PassengerCar"), TEXT("Flatcar_Crates"), TEXT("Caboose") }, 3, true);
    BanditTrain->LateralCm = SideTrackCm;
    BanditTrain->Offset = -400.0;
    BanditTrain->SetActorHiddenInGame(true);

    // A lantern on the player's car, so the tunnel is dark but not black.
    Lantern = NewObject<UPointLightComponent>(Train);
    Lantern->SetMobility(EComponentMobility::Movable);
    Lantern->SetupAttachment(Train->GetRootComponent());
    Lantern->SetRelativeLocation(FVector(PlayerForwardCm + 250.0f, 0.0f, AFGTrain::RoofCm + 230.0f));
    Lantern->SetIntensityUnits(ELightUnits::Candelas);
    Lantern->SetIntensity(0.0f);
    Lantern->SetLightColor(FLinearColor(1.0f, 0.62f, 0.28f));
    Lantern->SetAttenuationRadius(2600.0f);
    Lantern->SetCastShadows(false);
    Lantern->RegisterComponent();
}

FTransform AFGPresentation::AnchorTransform(const FFGAnchor& Anchor) const
{
    // Things on the cars belong to those cars. Each car sits on its own bit of track, so on a curve the boxcar
    // swings away from straight ahead, and anything placed in plain world coordinates slid across its roof.
    switch (Anchor.Kind)
    {
    case EFGAnchorKind::OwnCar:
        return World->TrackWorld(Train->CarCentre(Anchor.Car), 0.0f);
    case EFGAnchorKind::BanditCar:
        return World->TrackWorld(BanditTrain->Offset + BanditTrain->CarCentre(Anchor.Car), BanditTrain->LateralCm);
    default:
        return FTransform::Identity;
    }
}

bool AFGPresentation::IsLineReady() const
{
    return bAuthority || (World && World->NextChunkIndex() > 0);
}

void AFGPresentation::RingBell()
{
    if (USkeletalMeshComponent* Rig = World ? World->FindRig(TEXT("StationBell")) : nullptr)
    {
        if (UAnimSequence* Ring = FGAssets::Anim(TEXT("setpieces"), TEXT("SK_StationBell"), TEXT("ring"))) { Rig->PlayAnimation(Ring, false); }
    }
}

void AFGPresentation::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
    AFGGameState* GS = GetWorld()->GetGameState<AFGGameState>();
    if (!GS || !World) { return; }
    if (bAuthority)
    {
        CurrentSpeed = GS->Clock.Speed;      // the game mode has already moved the line this frame
    }
    else
    {
        FollowHost(GS, DeltaTime);
    }
    Train->Place(World, CurrentSpeed);
    if (!BanditTrain->IsHidden()) { BanditTrain->Place(World, CurrentSpeed); }

    // As modelled the station bell hangs 2.5 m up, far below a player on the roof. A taller post puts it near eye level.
    if (GS->bBellRaised)
    {
        USkeletalMeshComponent* Rig = World->FindRig(TEXT("StationBell"));
        if (Rig && Rig != RaisedBell)
        {
            Rig->SetRelativeScale3D(FVector(2.3f));
            RaisedBell = Rig;
        }
    }

    // Steam from the stack
    SteamTimer -= DeltaTime;
    if (SteamTimer <= 0.0f)
    {
        SteamTimer = CurrentSpeed > 1.0f ? 0.11f : 0.5f;
        if (USkeletalMeshComponent* Loco = Train->Car(0))
        {
            const FVector Stack = Loco->GetComponentTransform().TransformPosition(FVector(0.0f, 330.0f, 440.0f));
            if (AFGFx* Puff = AFGFx::Spawn(GetWorld(), TEXT("fx"), TEXT("SM_Puff_Steam"), FTransform(FRotator(0, FMath::FRandRange(0.f, 360.f), 0), Stack, FVector(0.3f)),
                1.8f, FVector(-CurrentSpeed * 80.0f, FMath::FRandRange(-40.f, 40.f), 520.0f), 1.3f))
            {
                Puff->Glow(FLinearColor(0.75f, 0.72f, 0.68f), 0.45f);
            }
        }
    }
    TickSound(GS, DeltaTime);
    TickSky(GS, DeltaTime);
    TickTest(GS, DeltaTime);
}

void AFGPresentation::TickTest(AFGGameState* GS, float DeltaTime)
{
    const TCHAR* Who = GetNetMode() == NM_Client ? TEXT("guest") : (GetNetMode() == NM_ListenServer ? TEXT("host") : TEXT("solo"));
    if (bPerf)
    {
        // Average fps, worst frame, how many frames went over 50 ms.
        const float Ms = FApp::GetDeltaTime() * 1000.0f;
        PerfSum += Ms; PerfWorst = FMath::Max(PerfWorst, Ms); PerfSlow += Ms > 50.0f; ++PerfFrames;
        if (PerfSum >= 2000.0f)
        {
            int32 Bandits = 0;
            for (TActorIterator<AActor> It(GetWorld()); It; ++It) { Bandits += It->GetClass()->GetName() == TEXT("FGBandit"); }
            UE_LOG(LogTemp, Log, TEXT("IronHorse perf (%s): %.0f fps, worst %.0f ms, %d slow frames, phase %d, %.2f km, bandits %d, chunk %d"), Who, PerfFrames * 1000.0f / PerfSum, PerfWorst, PerfSlow,
                int32(GS->Phase), GS->DistanceM / 1000.0, Bandits, World->NextChunkIndex());
            PerfSum = PerfWorst = 0.0f; PerfSlow = PerfFrames = 0;
        }
    }
    if (ShotEvery > 0.0f)
    {
        ShotTimer -= DeltaTime;
        if (ShotTimer <= 0.0f)
        {
            ShotTimer = ShotEvery;
            FScreenshotRequest::RequestScreenshot(FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / FString::Printf(TEXT("Screenshots/fg_%s_%03d.png"), Who, ShotIndex++)), true, false);
        }
    }
}

void AFGPresentation::FollowHost(AFGGameState* GS, float DeltaTime)
{
    World->KeepBehindM = GS->bVersus ? 450.0 : 70.0;
    if (GS->LineEpoch != Epoch)
    {
        // Ride again on the host: the old line is torn up there, so here too.
        World->ResetLine();
        Epoch = GS->LineEpoch;
        bHaveS = false;
    }
    if (GS->DefsChecksum != 0 && GS->DefsChecksum != World->DefsChecksum() && DefsWarned++ == 0)
    {
        UE_LOG(LogTemp, Error, TEXT("IronHorse: this copy's chunks.json differs from the host's. Both players need the same build."));
    }
    TArray<const FFGChunkRec*, TInlineAllocator<FG::ChunkRingSize>> Recs;
    for (const FFGChunkRec& Rec : GS->ChunkRing)
    {
        if (Rec.Epoch == Epoch && Rec.Index >= World->NextChunkIndex()) { Recs.Add(&Rec); }
    }
    Recs.Sort([](const FFGChunkRec& A, const FFGChunkRec& B) { return A.Index < B.Index; });
    for (const FFGChunkRec* Rec : Recs) { World->ApplyChunk(*Rec); }
    for (const FFGLeanRec& Rec : GS->LeanRing)
    {
        if (Rec.Epoch == Epoch) { World->AddLeanObstacleAt(Rec.Id, Rec.S, Rec.Side); }
    }

    // Carry the train on between updates, and ease onto the host's figure rather than jump to it.
    CurrentSpeed = GS->Clock.Speed;
    const double Target = GS->Clock.S + GS->Clock.Speed * FMath::Max(0.0, GS->GetServerWorldTimeSeconds() - GS->Clock.At);
    if (!bHaveS || FMath::Abs(Target - LocalS) > 15.0)
    {
        LocalS = Target;
        bHaveS = true;
    }
    else
    {
        LocalS += CurrentSpeed * DeltaTime;
        LocalS += (Target - LocalS) * FMath::Min(1.0, 3.0 * DeltaTime);
    }
    World->SetDistance(LocalS);

    BanditTrain->SetActorHiddenInGame(!GS->bBanditTrainVisible);
    BanditTrain->Offset = GS->bBanditTrainVisible && !BanditTrain->IsHidden()
        ? FMath::FInterpTo(BanditTrain->Offset, double(GS->BanditTrainOffset), double(DeltaTime), 8.0)
        : double(GS->BanditTrainOffset);
}

void AFGPresentation::TickSound(AFGGameState* GS, float DeltaTime)
{
    const float Rumble = FMath::Min(CurrentSpeed / FG::CruiseSpeed, 1.4f);
    if (TrainLoop) { TrainLoop->SetVolumeMultiplier(0.15f + 0.85f * Rumble); TrainLoop->SetPitchMultiplier(0.6f + 0.5f * Rumble); }

    // Music: day ride, night heist, or the duel. All three keep playing and only the volumes move, so the change
    // is a crossfade and never a restart.
    const EFGPhase Phase = GS->Phase;
    const int32 Wanted = Phase == EFGPhase::Showdown ? 2 : (GS->NightTarget > 0.5f ? 1 : 0);
    for (int32 i = 0; i < 3; ++i)
    {
        const float Loud = Phase == EFGPhase::Result ? 0.25f : (Phase == EFGPhase::Ride || Phase == EFGPhase::Showdown ? 0.55f : 0.4f);
        MusicLevel[i] = FMath::FInterpConstantTo(MusicLevel[i], i == Wanted ? Loud : 0.0f, DeltaTime, 0.35f);
        if (Music[i]) { Music[i]->SetVolumeMultiplier(FMath::Max(MusicLevel[i], 0.001f)); }
    }
}

void AFGPresentation::TickSky(AFGGameState* GS, float DeltaTime)
{
    if (!bSkyCaptured && Sky && GetWorld()->GetTimeSeconds() > 1.0f)
    {
        bSkyCaptured = true;        // once, after the atmosphere has rendered a few frames
        Sky->GetLightComponent()->RecaptureSky();
    }
    const bool bDark = World->MetresTo(TEXT("dark")) == 0.0f;
    Darkness = FMath::FInterpTo(Darkness, bDark ? 1.0f : 0.0f, DeltaTime, 2.5f);
    if (!bOwnSky)
    {
        if (Sun) { Sun->GetLightComponent()->SetIntensity(SunIntensity * (1.0f - 0.97f * Darkness)); }
        if (Sky) { Sky->GetLightComponent()->SetIntensity(1.3f * (1.0f - 0.9f * Darkness)); }
        if (Lantern) { Lantern->SetIntensity(Darkness * 320.0f); }
        return;
    }
    // The day runs with the lap (the host decides how far: Dusk), night after a boss falls, morning after the next.
    if (GS->LineEpoch != SkyEpoch) { SkyEpoch = GS->LineEpoch; Night = GS->NightTarget; }        // ride again: back to the afternoon at once
    Night = FMath::FInterpConstantTo(Night, GS->NightTarget, DeltaTime, 0.12f);
    const float Dusk = GS->Dusk;

    const float Pitch = FMath::Lerp(FMath::Lerp(-16.0f, -2.5f, Dusk), -38.0f, Night);
    // The sun belongs to the landscape: on a curve it swings round with the mesas, it does not ride along with the camera.
    const float Yaw = GS->SunChainYaw + World->WorldYaw() + 25.0f * Night;
    const FLinearColor DayColor = FMath::Lerp(FLinearColor(1.0f, 0.80f, 0.58f), FLinearColor(1.0f, 0.42f, 0.18f), Dusk);
    const FLinearColor Color = FMath::Lerp(DayColor, FLinearColor(0.45f, 0.58f, 1.0f), Night);
    const float Intensity = FMath::Lerp(FMath::Lerp(SunIntensity, SunIntensity * 0.6f, Dusk), 0.7f, Night);
    UDirectionalLightComponent* SunComp = Cast<UDirectionalLightComponent>(Sun->GetLightComponent());
    Sun->SetActorRotation(FRotator(Pitch, Yaw, 0.0f));
    SunComp->SetLightColor(Color);
    SunComp->SetIntensity(Intensity * (1.0f - 0.97f * Darkness));
    const bool bSunInSky = Night < 0.5f;       // at night the same light is the moon, and must not light the atmosphere up like a sun
    if (SunComp->IsUsedAsAtmosphereSunLight() != bSunInSky) { SunComp->SetAtmosphereSunLight(bSunInSky); }
    Sky->GetLightComponent()->SetIntensity(FMath::Lerp(1.3f, 0.5f, Night) * (1.0f - 0.9f * Darkness));
    if (Fog)
    {
        Fog->GetComponent()->SetFogInscatteringColor(FMath::Lerp(FMath::Lerp(FLinearColor(0.85f, 0.55f, 0.32f), FLinearColor(0.9f, 0.35f, 0.15f), Dusk), FLinearColor(0.02f, 0.035f, 0.08f), Night));
    }
    Lantern->SetIntensity(FMath::Max(Darkness * 320.0f, Night * 140.0f));
    // The sky light is a one-off capture (cheap). Take a new one whenever the sky has visibly moved on.
    const float Key = Dusk * 6.0f + Night * 8.0f;
    if (FMath::Abs(Key - SkyKey) > 1.0f && GetWorld()->GetTimeSeconds() > 1.0f)
    {
        SkyKey = Key;
        Sky->GetLightComponent()->RecaptureSky();
    }
}
