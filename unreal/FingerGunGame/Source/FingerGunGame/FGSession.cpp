#include "FGSession.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "FGAssets.h"
#include "GameFramework/PlayerController.h"
#include "IPAddress.h"
#include "Kismet/GameplayStatics.h"
#include "SocketSubsystem.h"
#if WITH_EDITOR
#include "AssetCompilingManager.h"
#endif

void UFGSession::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    if (GEngine)
    {
        NetworkFailureHandle = GEngine->OnNetworkFailure().AddUObject(this, &UFGSession::OnNetworkFailure);
        TravelFailureHandle = GEngine->OnTravelFailure().AddUObject(this, &UFGSession::OnTravelFailure);
    }
}

void UFGSession::Deinitialize()
{
    if (GEngine)
    {
        GEngine->OnNetworkFailure().Remove(NetworkFailureHandle);
        GEngine->OnTravelFailure().Remove(TravelFailureHandle);
    }
    Super::Deinitialize();
}

UFGSession* UFGSession::Get(const UObject* WorldContext)
{
    const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
    const UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
    return GI ? GI->GetSubsystem<UFGSession>() : nullptr;
}

void UFGSession::Preload()
{
    if (bPreloaded) { return; }
    bPreloaded = true;
    const double Start = FPlatformTime::Seconds();
    for (const FGAssets::FEntry& Entry : FGAssets::Manifest())
    {
        if (Entry.Class == TEXT("StaticMesh") || Entry.Class == TEXT("SkeletalMesh") || Entry.Class == TEXT("AnimSequence") || Entry.Class == TEXT("SoundWave"))
        {
            if (UObject* Asset = LoadObject<UObject>(nullptr, *(Entry.Path + TEXT(".") + Entry.Name), nullptr, LOAD_NoWarn)) { Preloaded.Add(Asset); }
        }
    }
#if WITH_EDITOR
    FAssetCompilingManager::Get().FinishAllCompilation();
#endif
    UE_LOG(LogTemp, Log, TEXT("IronHorse: preloaded %d assets in %.1f s"), Preloaded.Num(), FPlatformTime::Seconds() - Start);
}

void UFGSession::Host(const UObject* WorldContext, bool bVersus)
{
    LastError.Reset();
    UGameplayStatics::OpenLevel(WorldContext, FName(TEXT("/Game/Levels/IronHorse")), true, bVersus ? TEXT("listen?versus") : TEXT("listen"));
}

void UFGSession::Leave(const UObject* WorldContext)
{
    JoiningAddress.Reset();
    UGameplayStatics::OpenLevel(WorldContext, FName(TEXT("/Game/Levels/IronHorse")), true);
}

void UFGSession::Join(const UObject* WorldContext, const FString& Address)
{
    const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
    APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
    if (!PC || Address.IsEmpty()) { return; }
    LastError.Reset();
    JoiningAddress = Address;
    UE_LOG(LogTemp, Log, TEXT("IronHorse: joining %s"), *Address);
    PC->ClientTravel(Address, TRAVEL_Absolute);
}

void UFGSession::OnNetworkFailure(UWorld*, UNetDriver*, ENetworkFailure::Type Type, const FString& Message)
{
    // The engine takes us back to the default level (solo) by itself. All that is left is to say why.
    switch (Type)
    {
    case ENetworkFailure::ConnectionTimeout:
        Fail(JoiningAddress.IsEmpty() ? TEXT("THE CONNECTION TIMED OUT") : FString::Printf(TEXT("NO ANSWER FROM %s"), *JoiningAddress));
        break;
    case ENetworkFailure::ConnectionLost:
    case ENetworkFailure::FailureReceived:
        Fail(Message.Contains(TEXT("full"), ESearchCase::IgnoreCase) ? TEXT("THAT TRAIN IS FULL (2 PLAYERS)") : TEXT("LOST THE CONNECTION TO THE OTHER PLAYER"));
        break;
    default:
        Fail(TEXT("THE CONNECTION FAILED"));
        break;
    }
}

void UFGSession::OnTravelFailure(UWorld*, ETravelFailure::Type, const FString&)
{
    Fail(JoiningAddress.IsEmpty() ? TEXT("COULD NOT CONNECT") : FString::Printf(TEXT("COULD NOT REACH %s"), *JoiningAddress));
}

void UFGSession::Fail(const FString& Message)
{
    UE_LOG(LogTemp, Warning, TEXT("IronHorse: %s"), *Message);
    LastError = Message;
    LastErrorAt = FPlatformTime::Seconds();
    JoiningAddress.Reset();
}

FString UFGSession::LocalAddress()
{
    static FString Cached;
    if (Cached.IsEmpty())
    {
        ISocketSubsystem* Sockets = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
        bool bCanBind = false;
        TSharedPtr<FInternetAddr> Addr;
        if (Sockets) { Addr = Sockets->GetLocalHostAddr(*GLog, bCanBind); }
        Cached = Addr.IsValid() && Addr->IsValid() ? Addr->ToString(false) : TEXT("?");
    }
    return Cached;
}
