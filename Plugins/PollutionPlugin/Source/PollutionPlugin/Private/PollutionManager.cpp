// Fill out your copyright notice in the Description page of Project Settings.


#include "PollutionManager.h"

// Sets default values
APollutionManager::APollutionManager()
{
 	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 1.0f;

	UseBufferA = true;
	isProcessing = false;
	GridSize = 1;
	SpillOverBorders = true;
	AreWindWeightsDirty = true;

	Directions.Add(SpreadDirection(1, 0, 1));
	Directions.Add(SpreadDirection(-1, 0, 1));
	Directions.Add(SpreadDirection(0, 1, 1));
	Directions.Add(SpreadDirection(0, -1, 1));

	float t = sqrt(2.0) / 2;
	Directions.Add(SpreadDirection(1, 1, t));
	Directions.Add(SpreadDirection(1, -1, t));
	Directions.Add(SpreadDirection(-1, 1, t));
	Directions.Add(SpreadDirection(-1, -1, t));

	Area = CreateDefaultSubobject<UBoxComponent>("Area");
	Area->SetLineThickness(30);
}

// Called when the game starts or when spawned
void APollutionManager::BeginPlay()
{
	Super::BeginPlay();
	Area->DestroyComponent();
	BufferA.Empty();
	BufferB.Empty();
	BufferA.SetNum(GridSize * GridSize * Pollutants.Num());
	BufferB.SetNum(GridSize * GridSize * Pollutants.Num());
}

// Called every frame
void APollutionManager::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (isProcessing) return;
	OperationStartTime = FDateTime::UtcNow();
	isProcessing = true;
	//sync buffers (must be in game thread to avoid Game thread from editing at wrong moment)
	TArray<float>* CopyFrom = UseBufferA ? &BufferA : &BufferB;
	TArray<float>* CopyTo = !UseBufferA ? &BufferA : &BufferB;
	FMemory::Memcpy(CopyTo->GetData(), CopyFrom->GetData(), CopyFrom->Num() * 4);
	//*CopyTo = *CopyFrom;

	WorkingEditQueue = EditQueue;
	EditQueue.Empty();

	if (AreWindWeightsDirty) UpdateWindDirection();

	if (!BufferToLoad.IsEmpty()) {
		BufferA = BufferToLoad;
		BufferB = BufferToLoad;
		BufferToLoad.Empty();
	}

	AsyncTask(ENamedThreads::AnyHiPriThreadNormalTask, [&]() {

		for (auto a : WorkingEditQueue) {
			auto i = GetPollutantIndexFromName(a.Pollutant);
			if (i == -1) continue;
			GetPoint(i, a.Position, true) = FMath::Max(0, GetPoint(i, a.Position, true) + a.Modifier);
		}
		WorkingEditQueue.Empty();

		int32 SubdivisionWidth = (GridSize + 2) / 3;
		int32 SubCellCount = SubdivisionWidth * SubdivisionWidth;
		//for each pollutant (cpus don't have enough threads for this to need to be a parallel for....)
		for (int p = 0; p < Pollutants.Num(); p++) {
			//split into 3x3 sub-grids to avoid thread conflicts.
			for (int32 i = 0; i < 9; i++) {
				ParallelFor(SubCellCount, [&](int32 c) {
					ProcessGridPoint(p, IndexToPos(c, SubdivisionWidth) * 3 + (IndexToPos(i, 3)));
					});
			}
		}
		AsyncTask(ENamedThreads::GameThread, [&]() {
			isProcessing = false;
			UseBufferA = !UseBufferA;
			float TimeTakenMs = (FDateTime::UtcNow() - OperationStartTime).GetTotalMilliseconds();
			if(TimeTakenMs >= 1000) 
				UE_LOG(LogTemp, Warning, TEXT("Finished processing pollution in %f ms. One or more steps were skipped. Consider lowering the grid size."), TimeTakenMs);
			});
		});
}

void APollutionManager::OnConstruction(const FTransform& Transform)
{
	Area->SetBoxExtent(FVector(WorldExtent.X, WorldExtent.Y, 10000.0));
}

FIntPoint APollutionManager::GetGridPointFromLocation(FVector Location)
{
	auto l = GetActorTransform().InverseTransformPosition(Location);
	auto x = FMath::GetMappedRangeValueClamped(FVector2D(-WorldExtent.X, WorldExtent.X), FVector2D(0.0, GridSize), l.X);
	auto y = FMath::GetMappedRangeValueClamped(FVector2D(-WorldExtent.Y, WorldExtent.Y), FVector2D(0.0, GridSize), l.Y);
	return FIntPoint(round(x), round(y));
}

FVector APollutionManager::GetLocationFromGridPoint(FIntPoint Position, double Z)
{
	if (!IsPointValid(Position)) return FVector();
	auto x = FMath::GetMappedRangeValueUnclamped(FVector2D(0.0, GridSize), FVector2D(-WorldExtent.X, WorldExtent.X), Position.X);
	auto y = FMath::GetMappedRangeValueUnclamped(FVector2D(0.0, GridSize), FVector2D(-WorldExtent.Y, WorldExtent.Y), Position.Y);
	return GetActorTransform().TransformPosition(FVector(x, y, Z));
}

float APollutionManager::GetPollutionValueAtLocation(FVector Location, FName Pollutant)
{
	auto pos = GetGridPointFromLocation(Location);
	return GetPollutionValueFromGridPoint(pos, Pollutant);
}

float APollutionManager::GetPollutionValueFromGridPoint(FIntPoint Position, FName Pollutant)
{
	auto index = GetPollutantIndexFromName(Pollutant);
	if (index == -1) return -1;
	return GetPoint(index, Position, false);
}

void APollutionManager::ModifyPollutionAtLocation(FVector Location, FQueueEditPollution Edit)
{
	auto pos = GetGridPointFromLocation(Location);
	ModifyPollutionAtGridPoint(pos, Edit);
}

void APollutionManager::ModifyPollutionAtGridPoint(FIntPoint Position, FQueueEditPollution Edit)
{
	Edit.Position = Position;
	EditQueue.Add(Edit);
}

void APollutionManager::GetBuffer(TArray<float>& Out)
{
	Out = UseBufferA ? BufferA : BufferB;
}

UTexture2D* APollutionManager::GetPollutionTexture(FName Pollutant, UTexture2D* TextureToUpdate)
{
	if (BufferA.Num() == 0) return nullptr;
	auto PollutantIndex = GetPollutantIndexFromName(Pollutant);
	if (PollutantIndex == -1) return nullptr;

	return UpdateTextureFrom32BitFloat((UseBufferA ? BufferB : BufferA).GetData() + (PollutantIndex * GridSize * GridSize), GridSize, GridSize, TextureToUpdate);

}

void APollutionManager::ProcessGridPoint(int32 PollutantIndex, FIntPoint Position)
{
	if (!IsPointValid(Position)) return;

	float toSpread = GetPoint(PollutantIndex, Position, false) * Pollutants[PollutantIndex].DispersionRate;

	//spread
	float NotSpread = 0;
	FIntPoint TargetPos;
	for (auto i : Directions) {
		TargetPos = Position + i.Direction;
		if (!IsPointValid(TargetPos)) {
			if (!SpillOverBorders) NotSpread += toSpread * i.FinalPercentage;
			continue;
		}

		GetPoint(PollutantIndex, TargetPos, true) += toSpread * i.FinalPercentage;
	}
	GetPoint(PollutantIndex, Position, true) -= (toSpread - NotSpread);

	//decay
	float ToDecay = FMath::Min(GetPoint(PollutantIndex, Position, false) * Pollutants[PollutantIndex].DecayRate, Pollutants[PollutantIndex].MaxDecayPerTickPerCell);
	GetPoint(PollutantIndex, Position, true) -= ToDecay;
}

FIntPoint APollutionManager::IndexToPos(int32 Index, int32 GridWidth)
{
	if (GridWidth == -1) GridWidth = GridSize;
	return FIntPoint(Index % GridWidth, Index / GridWidth);
}

int32 APollutionManager::PosToIndex(int32 x, int32 y, int32 GridWidth)
{
	if (GridWidth == -1) GridWidth = GridSize;
	return y * GridWidth + x;
}

void APollutionManager::SetWind(FVector2D Direction, float Strength)
{
	if (Strength == 0) WindDirection = FVector2D();
	else WindDirection = Direction * (FMath::Clamp(Strength, 0.0f, 1.0f) / Direction.Length());
	AreWindWeightsDirty = true;
}

int32 APollutionManager::GetPollutantIndexFromName(FName Name)
{
	for (int32 i = 0; i < Pollutants.Num(); i++) {
		if (Pollutants[i].Name == Name)
			return i;
	}
	UE_LOG(LogTemp, Error, TEXT("Invalid pollutant name was used."));
	return -1;
}

inline int32 APollutionManager::PosToIndex(FIntPoint& pos, int32 GridWidth)
{
	return PosToIndex(pos.X, pos.Y, GridWidth);
}

inline float& APollutionManager::GetPoint(int32 PollutantIndex, FIntPoint& pos, bool write)
{
	return ((UseBufferA == write) ? BufferB : BufferA)[PosToIndex(pos) + (GridSize * GridSize * PollutantIndex)];
}

inline bool APollutionManager::IsPointValid(const FIntPoint& pos)
{
	return pos.X >= 0 && pos.Y >= 0 && pos.X < GridSize && pos.Y < GridSize;
}

UTexture2D* APollutionManager::CreateTextureFrom32BitFloat(float* data, int width, int height) {
	UTexture2D* Texture;
	Texture = UTexture2D::CreateTransient(width, height, PF_R32_FLOAT);
	if (!Texture) return nullptr;
#if WITH_EDITORONLY_DATA
	Texture->MipGenSettings = TMGS_NoMipmaps;
#endif
	Texture->NeverStream = true;
	Texture->SRGB = 0;
	Texture->LODGroup = TextureGroup::TEXTUREGROUP_Pixels2D;
	FTexture2DMipMap& Mip = Texture->PlatformData->Mips[0];
	void* Data = Mip.BulkData.Lock(LOCK_READ_WRITE);
	FMemory::Memcpy(Data, data, width * height * 4);
	Mip.BulkData.Unlock();
	Texture->UpdateResource();
	return Texture;
}

UTexture2D* APollutionManager::UpdateTextureFrom32BitFloat(float* data, int width, int height, UTexture2D* texture) {
	if (texture == nullptr)
		return CreateTextureFrom32BitFloat(data, width, height);
	FTexture2DMipMap& Mip = texture->PlatformData->Mips[0];
	void* Data = Mip.BulkData.Lock(LOCK_READ_WRITE);
	FMemory::Memcpy(Data, data, width * height * 4);
	Mip.BulkData.Unlock();
	texture->UpdateResource();
	return texture;
}

void APollutionManager::UpdateWindDirection()
{
	float tot = 0;
	for (SpreadDirection& i : Directions) {
		float w;
		if (WindDirection.Length() == 0) w = 1;
		else w = (i.Direction.X * WindDirection.X + i.Direction.Y * WindDirection.Y) / (FVector2D(i.Direction).Length() * WindDirection.Length());
		w = (w * 0.5) + 0.5;
		w = pow(w, 10 * WindDirection.Length());
		i.FinalPercentage = FMath::Lerp(1, w, WindDirection.Length()) * i.Weight;
		tot += i.FinalPercentage;
	}

	for (SpreadDirection& i : Directions)
		i.FinalPercentage = i.FinalPercentage / tot;

	AreWindWeightsDirty = false;
}

bool APollutionManager::LoadBuffer(TArray<float> Buffer)
{
	if (Buffer.Num() != BufferA.Num()) return false;
	BufferToLoad = Buffer;
	return true;
}

SpreadDirection::SpreadDirection()
{
	Direction = 0;
	Weight = 1.0f;
	FinalPercentage = 1.0f;
}

SpreadDirection::SpreadDirection(int x, int y, float weight)
{
	Direction = FIntPoint(x, y);
	Weight = weight;
	FinalPercentage = weight;
}
