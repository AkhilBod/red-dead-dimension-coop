#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "FGPlayerController.generated.h"

/**
 * The local seat at the screen: render settings, mouse, the main menu, typing in a host's address, and the result
 * screen's keys. Each player's own controller, on their own machine.
 */
UCLASS()
class FINGERGUNGAME_API AFGPlayerController : public APlayerController
{
    GENERATED_BODY()

public:
    virtual void BeginPlay() override;
    virtual void PlayerTick(float DeltaTime) override;

    UFUNCTION(Server, Reliable)
    void ServerVoteRideAgain();

    /** The host gives up waiting for a partner and rides alone. */
    UFUNCTION(Server, Reliable)
    void ServerRideAlone();

    /** A sound only this player hears: their own dodge, their hat coming back. */
    UFUNCTION(Client, Unreliable)
    void ClientSfx(FName Name, float Volume = 1.0f, float Pitch = 1.0f);

    // ---- the main menu: solo, on the title screen. Shoot an item (or click it), or arrow keys and Enter, or 1-5.
    enum EMenuItem : int32 { Ride, HostRide, HostVersus, Join, MenuItemCount };
    static FBox2D MenuBox(int32 Item);          // in 0..1 screen space
    static int32 MenuItemAt(FVector2D Aim);     // or -1
    static const TCHAR* MenuLabel(int32 Item);
    static const TCHAR* MenuNote(int32 Item);
    bool IsMenuOpen() const;
    /** A shot on the title screen. Returns true if it picked something. */
    bool MenuShot(FVector2D Aim);
    int32 MenuIndex = 0;

    bool bTypingAddress = false;
    FString Address;

private:
    void TickMenu();
    void TickAddressEntry();
    void Choose(int32 Item);
    FVector2D LastMenuAim = FVector2D(-1.0, -1.0);
};
