#include "FGTrackerInput.h"

#include "Common/UdpSocketBuilder.h"
#include "Engine/Texture2D.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Modules/ModuleManager.h"
#include "SocketSubsystem.h"
#include "Sockets.h"

namespace
{
    FSocket* MakeListener(const TCHAR* Name, int32 Port)
    {
        FSocket* S = FUdpSocketBuilder(Name).AsNonBlocking().AsReusable().BoundToPort(Port).WithReceiveBufferSize(1 << 20).Build();
        if (!S)
        {
            UE_LOG(LogTemp, Warning, TEXT("FGTrackerInput: could not bind UDP %d. Is another copy of the game running?"), Port);
        }
        return S;
    }

    void CloseSocket(FSocket*& S)
    {
        if (S)
        {
            S->Close();
            ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(S);
            S = nullptr;
        }
    }

    float ReadBigEndianFloat(const uint8* P)
    {
        const uint32 Bits = (uint32(P[0]) << 24) | (uint32(P[1]) << 16) | (uint32(P[2]) << 8) | uint32(P[3]);
        float F;
        FMemory::Memcpy(&F, &Bits, 4);
        return F;
    }
}

UFGTrackerInput::UFGTrackerInput()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

void UFGTrackerInput::BeginPlay()
{
    Super::BeginPlay();
    StateSocket = MakeListener(TEXT("FGState"), StatePort);
    CameraSocket = MakeListener(TEXT("FGCamera"), CameraPort);
    SendSocket = FUdpSocketBuilder(TEXT("FGCommands")).AsNonBlocking().Build();
}

void UFGTrackerInput::EndPlay(const EEndPlayReason::Type Reason)
{
    CloseSocket(StateSocket);
    CloseSocket(CameraSocket);
    CloseSocket(SendSocket);
    Super::EndPlay(Reason);
}

bool UFGTrackerInput::HasCameraPreview() const
{
    return CameraTexture && FPlatformTime::Seconds() - LastCameraTime < 2.0;
}

void UFGTrackerInput::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    PollState();
    PollCamera();

    // Unscaled time: Focus slows the world, not the player's hand.
    const float RealDelta = FApp::GetDeltaTime();
    bTrackerLive = FPlatformTime::Seconds() - LastStateTime < 1.0;
    if (bTrackerLive)
    {
        // 30 Hz in, frame rate out. Fast enough to add about one camera frame of lag, no more.
        const float A = 1.0f - FMath::Exp(-RealDelta * 40.0f);
        const float B = 1.0f - FMath::Exp(-RealDelta * 14.0f);
        State.AimX = FMath::Lerp(State.AimX, Raw.AimX, A);
        State.AimY = FMath::Lerp(State.AimY, Raw.AimY, A);
        State.Lean = FMath::Lerp(State.Lean, Raw.Lean, B);
        State.Duck = FMath::Lerp(State.Duck, Raw.Duck, B);
        State.BodySpeed = Raw.BodySpeed;
        State.bAimValid = Raw.bAimValid;
        State.bGunPose = Raw.bGunPose;
        State.bHolstered = Raw.bHolstered;
        State.bTracking = Raw.bTracking;
        State.bOffHandOpen = Raw.bOffHandOpen;
        NoPersonSeconds = Raw.bTracking ? 0.0f : NoPersonSeconds + RealDelta;
    }
    else
    {
        NoPersonSeconds = 0.0f;
    }
    TickMouse(RealDelta);
}

void UFGTrackerInput::TickMouse(float DeltaTime)
{
    const APawn* Pawn = Cast<APawn>(GetOwner());
    APlayerController* PC = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
    if (!PC)
    {
        return;
    }
    // The mouse button and R always work, tracker or not. Handy at the booth when someone is stuck.
    if (PC->WasInputKeyJustPressed(EKeys::LeftMouseButton))
    {
        float MX, MY;
        int32 W, H;
        PC->GetViewportSize(W, H);
        if (PC->GetMousePosition(MX, MY) && W > 0 && H > 0)
        {
            OnFire.Broadcast(FVector2D(MX / W, MY / H));
        }
    }
    if (PC->WasInputKeyJustPressed(EKeys::R))
    {
        OnReload.Broadcast();
    }
    if (bTrackerLive)
    {
        return;
    }
    float MX, MY;
    int32 W, H;
    PC->GetViewportSize(W, H);
    if (PC->GetMousePosition(MX, MY) && W > 0 && H > 0)
    {
        State.AimX = FMath::Clamp(MX / W, 0.0f, 1.0f);
        State.AimY = FMath::Clamp(MY / H, 0.0f, 1.0f);
    }
    const float LeanTarget = (PC->IsInputKeyDown(EKeys::D) ? 1.0f : 0.0f) - (PC->IsInputKeyDown(EKeys::A) ? 1.0f : 0.0f);
    const float DuckTarget = PC->IsInputKeyDown(EKeys::S) ? 1.0f : 0.0f;
    State.Lean = FMath::FInterpTo(State.Lean, LeanTarget, DeltaTime, 9.0f);
    State.Duck = FMath::FInterpTo(State.Duck, DuckTarget, DeltaTime, 11.0f);
    State.bAimValid = !PC->IsInputKeyDown(EKeys::H);
    State.bHolstered = PC->IsInputKeyDown(EKeys::H);
    State.bGunPose = true;
    State.bTracking = true;
    State.bOffHandOpen = PC->IsInputKeyDown(EKeys::F);
}

void UFGTrackerInput::PollState()
{
    if (!StateSocket)
    {
        return;
    }
    uint8 Buffer[2048];
    uint32 Pending = 0;
    TSharedRef<FInternetAddr> From = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->CreateInternetAddr();
    while (StateSocket->HasPendingData(Pending))
    {
        int32 Read = 0;
        if (!StateSocket->RecvFrom(Buffer, sizeof(Buffer), Read, *From) || Read <= 0)
        {
            break;
        }
        HandleOsc(Buffer, Read);
    }
}

void UFGTrackerInput::HandleOsc(const uint8* Data, int32 Size)
{
    // address\0 pad4, ",fff"\0 pad4, big-endian float32s
    int32 AddrEnd = 0;
    while (AddrEnd < Size && Data[AddrEnd] != 0) { ++AddrEnd; }
    if (AddrEnd == 0 || AddrEnd >= Size) { return; }
    const FString Address = FString::ConstructFromPtrSize(reinterpret_cast<const ANSICHAR*>(Data), AddrEnd);
    int32 Pos = (AddrEnd + 4) & ~3;
    if (Pos >= Size || Data[Pos] != ',') { return; }
    int32 TagEnd = Pos;
    while (TagEnd < Size && Data[TagEnd] != 0) { ++TagEnd; }
    const int32 NumTags = TagEnd - Pos - 1;
    int32 ArgPos = (TagEnd + 4) & ~3;
    TArray<float, TInlineAllocator<12>> Args;
    for (int32 i = 0; i < NumTags && ArgPos + 4 <= Size; ++i)
    {
        const uint8 Tag = Data[Pos + 1 + i];
        if (Tag == 'f')
        {
            Args.Add(ReadBigEndianFloat(Data + ArgPos));
        }
        else if (Tag == 'i')
        {
            const int32 V = int32((uint32(Data[ArgPos]) << 24) | (uint32(Data[ArgPos + 1]) << 16) | (uint32(Data[ArgPos + 2]) << 8) | uint32(Data[ArgPos + 3]));
            Args.Add(float(V));
        }
        ArgPos += 4;
    }

    if (Address == TEXT("/fg/state") && Args.Num() >= 10)
    {
        Raw.AimX = Stretch(Args[0]);
        Raw.AimY = Stretch(Args[1]);
        Raw.bAimValid = Args[2] > 0.5f;
        Raw.bGunPose = Args[3] > 0.5f;
        Raw.bHolstered = Args[4] > 0.5f;
        Raw.Lean = FMath::Clamp(Args[5], -1.0f, 1.0f);
        Raw.Duck = FMath::Clamp(Args[6], 0.0f, 1.0f);
        Raw.BodySpeed = Args[7];
        Raw.bTracking = Args[8] > 0.5f;
        Raw.bOffHandOpen = Args[9] > 0.5f;
        if (!bTrackerLive)
        {
            State.AimX = Raw.AimX;
            State.AimY = Raw.AimY;
        }
        LastStateTime = FPlatformTime::Seconds();
    }
    else if (Address == TEXT("/fg/fire") && Args.Num() >= 2)
    {
        OnFire.Broadcast(FVector2D(Stretch(Args[0]), Stretch(Args[1])));
    }
    else if (Address == TEXT("/fg/reload"))
    {
        OnReload.Broadcast();
    }
}

void UFGTrackerInput::PollCamera()
{
    if (!CameraSocket)
    {
        return;
    }
    // 'FGCM', frame id (u16 BE), part index, part count, then a slice of one JPEG.
    // macOS caps a UDP datagram at 9216 bytes, hence the slices.
    static uint8 Buffer[16384];
    uint32 Pending = 0;
    TSharedRef<FInternetAddr> From = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->CreateInternetAddr();
    while (CameraSocket->HasPendingData(Pending))
    {
        int32 Read = 0;
        if (!CameraSocket->RecvFrom(Buffer, sizeof(Buffer), Read, *From) || Read <= 8)
        {
            break;
        }
        if (Buffer[0] != 'F' || Buffer[1] != 'G' || Buffer[2] != 'C' || Buffer[3] != 'M')
        {
            continue;
        }
        const uint16 FrameId = uint16((Buffer[4] << 8) | Buffer[5]);
        const int32 Index = Buffer[6];
        const int32 Count = Buffer[7];
        if (Count == 0 || Index >= Count)
        {
            continue;
        }
        if (FrameId != CamFrameId || CamParts.Num() != Count)
        {
            CamFrameId = FrameId;
            CamPartsGot = 0;
            CamParts.Reset();
            CamParts.SetNum(Count);
        }
        if (CamParts[Index].Num() == 0)
        {
            CamParts[Index].Append(Buffer + 8, Read - 8);
            if (++CamPartsGot == Count)
            {
                DecodeCameraFrame();
                CamParts.Reset();
                CamPartsGot = 0;
            }
        }
    }
}

void UFGTrackerInput::DecodeCameraFrame()
{
    TArray<uint8> Jpeg;
    for (const TArray<uint8>& Part : CamParts)
    {
        Jpeg.Append(Part);
    }
    IImageWrapperModule& Module = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
    TSharedPtr<IImageWrapper> Wrapper = Module.CreateImageWrapper(EImageFormat::JPEG);
    TArray<uint8> Pixels;
    if (!Wrapper.IsValid() || !Wrapper->SetCompressed(Jpeg.GetData(), Jpeg.Num()) || !Wrapper->GetRaw(ERGBFormat::BGRA, 8, Pixels))
    {
        return;
    }
    const int32 W = Wrapper->GetWidth();
    const int32 H = Wrapper->GetHeight();
    if (!CameraTexture || CameraTexture->GetSizeX() != W || CameraTexture->GetSizeY() != H)
    {
        CameraTexture = UTexture2D::CreateTransient(W, H, PF_B8G8R8A8);
        if (!CameraTexture)
        {
            return;
        }
        CameraTexture->SRGB = true;
        CameraTexture->Filter = TF_Bilinear;
    }
    FTexture2DMipMap& Mip = CameraTexture->GetPlatformData()->Mips[0];
    void* Dest = Mip.BulkData.Lock(LOCK_READ_WRITE);
    FMemory::Memcpy(Dest, Pixels.GetData(), FMath::Min<int64>(Pixels.Num(), int64(W) * H * 4));
    Mip.BulkData.Unlock();
    CameraTexture->UpdateResource();
    LastCameraTime = FPlatformTime::Seconds();
}

void UFGTrackerInput::SendOsc(const char* Address, const TArray<float>& Args)
{
    if (!SendSocket)
    {
        return;
    }
    TArray<uint8> Packet;
    auto AppendPadded = [&Packet](const char* Str, int32 Len)
    {
        Packet.Append(reinterpret_cast<const uint8*>(Str), Len);
        const int32 Pad = 4 - (Len % 4);
        for (int32 i = 0; i < Pad; ++i) { Packet.Add(0); }
    };
    AppendPadded(Address, FCStringAnsi::Strlen(Address));
    FAnsiString Tags(",");
    for (int32 i = 0; i < Args.Num(); ++i) { Tags += 'f'; }
    AppendPadded(*Tags, Tags.Len());
    for (const float V : Args)
    {
        uint32 Bits;
        FMemory::Memcpy(&Bits, &V, 4);
        Packet.Add(uint8(Bits >> 24));
        Packet.Add(uint8(Bits >> 16));
        Packet.Add(uint8(Bits >> 8));
        Packet.Add(uint8(Bits));
    }
    TSharedRef<FInternetAddr> To = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->CreateInternetAddr();
    bool bValid = false;
    To->SetIp(TEXT("127.0.0.1"), bValid);
    To->SetPort(CommandPort);
    int32 Sent = 0;
    SendSocket->SendTo(Packet.GetData(), Packet.Num(), Sent, *To);
}

void UFGTrackerInput::SendCalibBegin() { SendOsc("/fg/calib/begin", {}); }
void UFGTrackerInput::SendCalibTarget(float X, float Y) { SendOsc("/fg/calib/target", { X, Y }); }
void UFGTrackerInput::SendRecenter() { SendOsc("/fg/recenter", {}); }
