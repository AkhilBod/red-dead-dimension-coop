#include "PhysicalInputComponent.h"

UPhysicalInputComponent::UPhysicalInputComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
}

void UPhysicalInputComponent::SetLean(float Value)
{
    InputState.LeanX = FMath::Clamp(Value, -1.0f, 1.0f);
}

void UPhysicalInputComponent::SetHeight(float Value)
{
    InputState.Height = FMath::Clamp(Value, 0.0f, 1.0f);
}

void UPhysicalInputComponent::SetShoot(bool bPressed)
{
    if (bPressed && !InputState.bShoot)
    {
        OnShootPressed.Broadcast();
    }

    InputState.bShoot = bPressed;
}

void UPhysicalInputComponent::SetReload(bool bPressed)
{
    if (bPressed && !InputState.bReload)
    {
        OnReloadPressed.Broadcast();
    }

    InputState.bReload = bPressed;
}

float UPhysicalInputComponent::GetLean() const
{
    return InputState.LeanX;
}

float UPhysicalInputComponent::GetHeight() const
{
    return InputState.Height;
}

bool UPhysicalInputComponent::IsShootPressed() const
{
    return InputState.bShoot;
}

bool UPhysicalInputComponent::IsReloadPressed() const
{
    return InputState.bReload;
}