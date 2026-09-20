#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "FGHud.generated.h"

class UFont;

/** Canvas HUD: crosshair, cylinder, hats, pictogram prompts, the tracker's camera preview, the result poster. */
UCLASS()
class FINGERGUNGAME_API AFGHud : public AHUD
{
    GENERATED_BODY()

public:
    AFGHud();
    virtual void DrawHUD() override;

private:
    UPROPERTY()
    TObjectPtr<UFont> Font;

    float U = 1.0f;     // UI scale: 1 at 1080p

    void Box(float X, float Y, float W, float H, FLinearColor Color);
    void Text(const FString& Str, float X, float Y, int32 Size, FLinearColor Color, bool bCentre = true, bool bShadow = true);
    void Ring(FVector2D C, float Radius, float Thickness, FLinearColor Color, int32 Segments = 28);
    void Hat(float X, float Y, float S, FLinearColor Color);
};
