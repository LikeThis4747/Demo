// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file ZeroEscapeGenerationContractTests.cpp
 * 职责：保留与具体单层 Room 数据无关的 Transform 组合和随机子流隔离回归。
	 * 边界：Profile 解析与 Hash 由 ZeroEscapeMultiFloorDataContractTests 覆盖；
 *       多层求解由 ZeroEscapeMultiFloorLayoutTests 覆盖。
 */

#include "PCG/ZeroEscapeGenerationAssets.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Containers/StaticArray.h"
#include "Misc/AutomationTest.h"

#include "PCG/ZeroEscapeGenerationCore.h"

namespace ZeroEscape::LevelGeneration::Tests
{
	namespace ContractTestsPrivate
	{
		constexpr double TransformTolerance = 0.01;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FZeroEscapeTransformCompositionTest,
		"Demo.PCG.Unit.TransformComposition",
		EAutomationTestFlags_ApplicationContextMask
			| EAutomationTestFlags::ProductFilter)

	bool FZeroEscapeTransformCompositionTest::RunTest(const FString& Parameters)
	{
		using namespace ContractTestsPrivate;
		(void)Parameters;
		const FTransform PivotCorrection(
			FRotator(0.0, -31.0, 0.0),
			FVector(27.0, -13.0, 8.0),
			FVector::OneVector);
		const FTransform CanonicalStructure(
			FRotator(0.0, 90.0, 0.0),
			FVector(600.0, 300.0, 5.0),
			FVector::OneVector);
		const FTransform GeneratorRoot(
			FRotator(0.0, 23.0, 0.0),
			FVector(-450.0, 810.0, 40.0),
			FVector::OneVector);
		const FTransform PresentationLocal = PivotCorrection * CanonicalStructure;
		const FTransform PresentationWorld = PresentationLocal * GeneratorRoot;

		const TStaticArray<FVector, 4> TestPoints = {
			FVector::ZeroVector,
			FVector(100.0, 0.0, 0.0),
			FVector(0.0, 70.0, 30.0),
			FVector(11.0, -7.0, 3.0) };
		for (const FVector& Point : TestPoints)
		{
			const FVector Expected = GeneratorRoot.TransformPosition(
				CanonicalStructure.TransformPosition(
					PivotCorrection.TransformPosition(Point)));
			TestTrue(
				TEXT("Pivot -> Canonical -> Root 的逐点结果必须与组合 Transform 一致"),
				PresentationWorld.TransformPosition(Point).Equals(
					Expected, TransformTolerance));
		}

		const FVector Probe = TestPoints[TestPoints.Num() - 1];
		const FVector WrongOrder = GeneratorRoot.TransformPosition(
			PivotCorrection.TransformPosition(
				CanonicalStructure.TransformPosition(Probe)));
		TestFalse(
			TEXT("夹具必须能识别 Pivot 与规范结构顺序被交换"),
			WrongOrder.Equals(
				PresentationWorld.TransformPosition(Probe), TransformTolerance));
		TestTrue(
			TEXT("合法组合必须保持有限 Unit Scale"),
			FGenerationCore::IsFiniteUnitScaleTransform(PresentationWorld));
		TestFalse(
			TEXT("表现绑定不得通过 Scale 适配素材尺寸"),
			FGenerationCore::IsFiniteUnitScaleTransform(
				FTransform(
					FQuat::Identity,
					FVector::ZeroVector,
					FVector(2.0, 1.0, 1.0))));
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FZeroEscapeRandomDomainIsolationTest,
		"Demo.PCG.Unit.Core.MultiFloorRandomDomainIsolation",
		EAutomationTestFlags_ApplicationContextMask
			| EAutomationTestFlags::ProductFilter)

	bool FZeroEscapeRandomDomainIsolationTest::RunTest(const FString& Parameters)
	{
		(void)Parameters;
		const TStaticArray<ERandomDomain, 11> Domains = {
			ERandomDomain::FloorCount,
			ERandomDomain::RequiredTwoFloorStairPlacement,
			ERandomDomain::AdditionalTwoFloorStairCount,
			ERandomDomain::AdditionalTwoFloorStairPlacement,
			ERandomDomain::ThreeFloorStairwellPlacement,
			ERandomDomain::HighCeilingRoomCount,
			ERandomDomain::HighCeilingRoomPlacement,
			ERandomDomain::PlayerPursuerSpawn,
			ERandomDomain::WfcLayout,
			ERandomDomain::HazardPopulationPlacement,
			ERandomDomain::ResourcePopulationPlacement };
		TSet<uint32> FirstValues;
		for (const ERandomDomain Domain : Domains)
		{
			FRandomStream First = FGenerationCore::MakeRandomStream(
				13579, Domain, 17);
			FRandomStream Replay = FGenerationCore::MakeRandomStream(
				13579, Domain, 17);
			for (int32 DrawIndex = 0; DrawIndex < 8; ++DrawIndex)
			{
				TestEqual(
					TEXT("同一稳定随机域和 salt 必须复现逐次抽样"),
					First.GetUnsignedInt(),
					Replay.GetUnsignedInt());
			}
			FRandomStream DomainProbe = FGenerationCore::MakeRandomStream(
				13579, Domain, 17);
			if (Domain == ERandomDomain::WfcLayout)
			{
				TestEqual(
					TEXT("移除版本字段后必须保留既有 WFC 派生 Seed"),
					DomainProbe.GetInitialSeed(),
					938292507);
			}
			FirstValues.Add(DomainProbe.GetUnsignedInt());
		}
		TestEqual(
			TEXT("所有职责随机域的首个派生值必须互不相同"),
			FirstValues.Num(),
			Domains.Num());
		return true;
	}
}

#endif // WITH_DEV_AUTOMATION_TESTS
