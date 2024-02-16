// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Runtime/Core/Public/Async/ParallelFor.h"
#include "Components/BoxComponent.h"
#include "Kismet/BlueprintAsyncActionBase.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/Texture2D.h"
#include "Async/Async.h"
#include "TextureResource.h"

#include "PollutionManager.generated.h"

USTRUCT(BlueprintType, Blueprintable)
struct POLLUTIONPLUGIN_API FQueueEditPollution {
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadWrite, Category = "Pollution")
	FName Pollutant;

	UPROPERTY(BlueprintReadWrite, Category = "Pollution")
	float Modifier;

	FIntPoint Position;
};

class SpreadDirection {
public:
	SpreadDirection();
	SpreadDirection(int x, int y, float weight);
	
	FIntPoint Direction;
	float Weight;
	float FinalPercentage;
};

USTRUCT(BlueprintType, Blueprintable)
struct POLLUTIONPLUGIN_API FPollutant {
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pollution")
	FName Name;
	
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pollution", meta = (ClampMin = "0", ClampMax = "1"))
	float DispersionRate;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pollution", meta = (ClampMin = "0", ClampMax = "1"))
	float DecayRate;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pollution")
	float MaxDecayPerTickPerCell;
};

UCLASS()
class POLLUTIONPLUGIN_API APollutionManager : public AActor
{
	GENERATED_BODY()
	
public:	
	// Sets default values for this actor's properties
	APollutionManager();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

public:	
	// Called every frame
	virtual void Tick(float DeltaTime) override;
	virtual void OnConstruction(const FTransform& Transform) override;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pollution")
		TArray<FPollutant> Pollutants;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pollution")
		int32 GridSize;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pollution")
		bool SpillOverBorders;
	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = "Pollution")
		UBoxComponent* Area;
	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "Pollution")
		FVector2D WorldExtent;
	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "Pollution")
		FVector2D WindDirection;

	UFUNCTION(BlueprintPure, Category = "Pollution")
	FIntPoint GetGridPointFromLocation(FVector Location);
	UFUNCTION(BlueprintPure, Category = "Pollution")
	FVector GetLocationFromGridPoint(FIntPoint Position, double Z = 0);
	UFUNCTION(BlueprintPure, Category = "Pollution")
	float GetPollutionValueAtLocation(FVector Location, FName Pollutant);
	UFUNCTION(BlueprintPure, Category = "Pollution")
	float GetPollutionValueFromGridPoint(FIntPoint Position, FName Pollutant);
	UFUNCTION(BlueprintCallable, Category = "Pollution")
	void ModifyPollutionAtLocation(FVector Location, FQueueEditPollution Edit);
	UFUNCTION(BlueprintCallable, Category = "Pollution")
	void ModifyPollutionAtGridPoint(FIntPoint Position, FQueueEditPollution Edit);
	UFUNCTION(BlueprintCallable, Category = "Pollution")
	void GetBuffer(TArray<float>& Out);
	UFUNCTION(BlueprintCallable, Category = "Pollution")
	UTexture2D* GetPollutionTexture(FName Pollutant, UTexture2D* TextureToUpdate = nullptr);
	UFUNCTION(BlueprintCallable, Category = "Pollution")
	void SetWind(FVector2D Direction, float Strength);
	UFUNCTION(BlueprintCallable, Category = "Pollution")
	bool LoadBuffer(TArray<float> Buffer);


	void ProcessGridPoint(int32 PollutantIndex, FIntPoint Position);
	FIntPoint IndexToPos(int32 Index, int32 GridWidth = -1);
	int32 PosToIndex(int32 x, int32 y, int32 GridWidth = -1);
	//returns -1 if invalid
	int32 GetPollutantIndexFromName(FName Name);
	inline int32 PosToIndex(FIntPoint& pos, int32 GridWidth = -1);
	inline float& GetPoint(int32 PollutantIndex, FIntPoint& pos, bool write);
	inline bool IsPointValid(const FIntPoint& pos);
	UTexture2D* CreateTextureFrom32BitFloat(float* data, int width, int height);
	UTexture2D* UpdateTextureFrom32BitFloat(float* data, int width, int height, UTexture2D* texture);
	void UpdateWindDirection();

	TArray<float> BufferA;
	TArray<float> BufferB;
	//will be set to true when B is the one being edited
	bool UseBufferA;
	bool isProcessing;
	TArray<SpreadDirection> Directions;
	TArray<FQueueEditPollution> EditQueue;
	TArray<FQueueEditPollution> WorkingEditQueue;
	FDateTime OperationStartTime;
	bool AreWindWeightsDirty;
	TArray<float> BufferToLoad;
};