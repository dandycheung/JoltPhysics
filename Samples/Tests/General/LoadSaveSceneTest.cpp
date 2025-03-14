// Jolt Physics Library (https://github.com/jrouwe/JoltPhysics)
// SPDX-FileCopyrightText: 2021 Jorrit Rouwe
// SPDX-License-Identifier: MIT

#include <TestFramework.h>

#include <Tests/General/LoadSaveSceneTest.h>
#include <External/Perlin.h>
#include <Jolt/ObjectStream/ObjectStreamOut.h>
#include <Jolt/ObjectStream/ObjectStreamIn.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/ScaledShape.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/TaperedCapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/CylinderShape.h>
#include <Jolt/Physics/Collision/Shape/TaperedCylinderShape.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>
#include <Jolt/Physics/Collision/Shape/MutableCompoundShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/HeightFieldShape.h>
#include <Jolt/Physics/Collision/Shape/TriangleShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/EmptyShape.h>
#include <Jolt/Physics/Collision/PhysicsMaterialSimple.h>
#include <Jolt/Physics/Constraints/DistanceConstraint.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Layers.h>
#include <Utils/Log.h>
#include <Utils/SoftBodyCreator.h>

JPH_IMPLEMENT_RTTI_VIRTUAL(LoadSaveSceneTest)
{
	JPH_ADD_BASE_CLASS(LoadSaveSceneTest, Test)
}

static const float cMaxHeight = 4.0f;

static MeshShapeSettings *sCreateMesh()
{
	const int n = 10;
	const float cell_size = 2.0f;

	// Create heights
	float heights[n + 1][n + 1];
	for (int x = 0; x <= n; ++x)
		for (int z = 0; z <= n; ++z)
			heights[x][z] = cMaxHeight * PerlinNoise3(float(x) / n, 0, float(z) / n, 256, 256, 256);

	// Create 'wall' around grid
	for (int x = 0; x <= n; ++x)
	{
		heights[x][0] += 2.0f;
		heights[x][n] += 2.0f;
	}

	for (int y = 1; y < n; ++y)
	{
		heights[0][y] += 2.0f;
		heights[n][y] += 2.0f;
	}

	// Create regular grid of triangles
	uint32 max_material_index = 0;
	TriangleList triangles;
	for (int x = 0; x < n; ++x)
		for (int z = 0; z < n; ++z)
		{
			float center = n * cell_size / 2;

			float x1 = cell_size * x - center;
			float z1 = cell_size * z - center;
			float x2 = x1 + cell_size;
			float z2 = z1 + cell_size;

			Float3 v1 = Float3(x1, heights[x][z], z1);
			Float3 v2 = Float3(x2, heights[x + 1][z], z1);
			Float3 v3 = Float3(x1, heights[x][z + 1], z2);
			Float3 v4 = Float3(x2, heights[x + 1][z + 1], z2);

			uint32 material_index = uint32((Vec3(v1) + Vec3(v2) + Vec3(v3) + Vec3(v4)).Length() / 4.0f / cell_size);
			max_material_index = max(max_material_index, material_index);
			triangles.push_back(Triangle(v1, v3, v4, material_index));
			triangles.push_back(Triangle(v1, v4, v2, material_index));
		}

	// Create materials
	PhysicsMaterialList materials;
	for (uint i = 0; i <= max_material_index; ++i)
		materials.push_back(new PhysicsMaterialSimple("Mesh Material " + ConvertToString(i), Color::sGetDistinctColor(i)));

	return new MeshShapeSettings(triangles, std::move(materials));
}

static HeightFieldShapeSettings *sCreateHeightField()
{
	const int n = 32;
	const float cell_size = 1.0f;

	// Create height samples
	float heights[n * n];
	for (int y = 0; y < n; ++y)
		for (int x = 0; x < n; ++x)
			heights[y * n + x] = cMaxHeight * PerlinNoise3(float(x) / n, 0, float(y) / n, 256, 256, 256);

	// Make a hole
	heights[2 * n + 2] = HeightFieldShapeConstants::cNoCollisionValue;

	// Make material indices
	uint8 max_material_index = 0;
	uint8 material_indices[Square(n - 1)];
	for (int y = 0; y < n - 1; ++y)
		for (int x = 0; x < n - 1; ++x)
		{
			uint8 material_index = uint8(round((Vec3(x * cell_size, 0, y * cell_size) - Vec3(n * cell_size / 2, 0, n * cell_size / 2)).Length() / 10.0f));
			max_material_index = max(max_material_index, material_index);
			material_indices[y * (n - 1) + x] = material_index;
		}

	// Create materials
	PhysicsMaterialList materials;
	for (uint8 i = 0; i <= max_material_index; ++i)
		materials.push_back(new PhysicsMaterialSimple("HeightField Material " + ConvertToString(uint(i)), Color::sGetDistinctColor(i)));

	// Create height field
	return new HeightFieldShapeSettings(heights, Vec3(-0.5f * cell_size * n, 0.0f, -0.5f * cell_size * n), Vec3(cell_size, 1.0f, cell_size), n, material_indices, materials);
}

Ref<PhysicsScene> LoadSaveSceneTest::sCreateScene()
{
	int color = 0;
	auto next_color = [&color]() { return Color::sGetDistinctColor(color++); };
	RVec3 pos(0, cMaxHeight, 0);
	auto next_pos = [&pos]() { pos += RVec3(0, 1.0f, 0); return pos; };

	// Create scene
	Ref<PhysicsScene> scene = new PhysicsScene();

	// A scaled mesh floor
	scene->AddBody(BodyCreationSettings(new ScaledShapeSettings(sCreateMesh(), Vec3(2.5f, 1.0f, 1.5f)), RVec3(0, 0, 0), Quat::sIdentity(), EMotionType::Static, Layers::NON_MOVING));

	// A heightfield floor
	scene->AddBody(BodyCreationSettings(sCreateHeightField(), RVec3(50, 0, 0), Quat::sIdentity(), EMotionType::Static, Layers::NON_MOVING));

	// Some simple primitives
	scene->AddBody(BodyCreationSettings(new TriangleShapeSettings(Vec3(-2, 0, 0), Vec3(0, 1, 0), Vec3(2, 0, 0), 0.0f, new PhysicsMaterialSimple("Triangle Material", next_color())), next_pos(), Quat::sRotation(Vec3::sAxisX(), 0.5f * JPH_PI), EMotionType::Static, Layers::NON_MOVING));
	scene->AddBody(BodyCreationSettings(new SphereShapeSettings(0.2f, new PhysicsMaterialSimple("Sphere Material", next_color())), next_pos(), Quat::sIdentity(), EMotionType::Dynamic, Layers::MOVING));
	scene->AddBody(BodyCreationSettings(new BoxShapeSettings(Vec3(0.2f, 0.2f, 0.4f), 0.01f, new PhysicsMaterialSimple("Box Material", next_color())), next_pos(), Quat::sIdentity(), EMotionType::Dynamic, Layers::MOVING));
	scene->AddBody(BodyCreationSettings(new CapsuleShapeSettings(1.5f, 0.2f, new PhysicsMaterialSimple("Capsule Material", next_color())), next_pos(), Quat::sRotation(Vec3::sAxisX(), 0.5f * JPH_PI), EMotionType::Dynamic, Layers::MOVING));
	scene->AddBody(BodyCreationSettings(new TaperedCapsuleShapeSettings(0.5f, 0.1f, 0.2f, new PhysicsMaterialSimple("Tapered Capsule Material", next_color())), next_pos(), Quat::sRotation(Vec3::sAxisZ(), 0.5f * JPH_PI), EMotionType::Dynamic, Layers::MOVING));
	scene->AddBody(BodyCreationSettings(new CylinderShapeSettings(0.5f, 0.2f, cDefaultConvexRadius, new PhysicsMaterialSimple("Cylinder Material", next_color())), next_pos(), Quat::sRotation(Vec3::sAxisX(), 0.5f * JPH_PI), EMotionType::Dynamic, Layers::MOVING));
	scene->AddBody(BodyCreationSettings(new TaperedCylinderShapeSettings(0.5f, 0.2f, 0.4f, cDefaultConvexRadius, new PhysicsMaterialSimple("Tapered Cylinder Material", next_color())), next_pos(), Quat::sRotation(Vec3::sAxisX(), 0.5f * JPH_PI), EMotionType::Dynamic, Layers::MOVING));
	scene->AddBody(BodyCreationSettings(new TaperedCylinderShapeSettings(0.5f, 0.4f, 0.0f, 0.0f, new PhysicsMaterialSimple("Cone Material", next_color())), next_pos(), Quat::sRotation(Vec3::sAxisX(), 0.5f * JPH_PI), EMotionType::Dynamic, Layers::MOVING));
	scene->AddBody(BodyCreationSettings(new EmptyShapeSettings(), next_pos(), Quat::sIdentity(), EMotionType::Dynamic, Layers::MOVING));

	// Compound with sub compound and rotation
	StaticCompoundShapeSettings *sub_compound = new StaticCompoundShapeSettings();
	sub_compound->AddShape(Vec3(0, 0.5f, 0), Quat::sRotation(Vec3::sAxisZ(), 0.5f * JPH_PI), new BoxShapeSettings(Vec3(0.5f, 0.1f, 0.2f), cDefaultConvexRadius, new PhysicsMaterialSimple("Compound Box Material", next_color())));
	sub_compound->AddShape(Vec3(0.5f, 0, 0), Quat::sRotation(Vec3::sAxisZ(), 0.5f * JPH_PI), new CylinderShapeSettings(0.5f, 0.2f, cDefaultConvexRadius, new PhysicsMaterialSimple("Compound Cylinder Material", next_color())));
	sub_compound->AddShape(Vec3(0, 0, 0.5f), Quat::sRotation(Vec3::sAxisX(), 0.5f * JPH_PI), new TaperedCapsuleShapeSettings(0.5f, 0.1f, 0.2f, new PhysicsMaterialSimple("Compound Tapered Capsule Material", next_color())));
	StaticCompoundShapeSettings *compound_shape = new StaticCompoundShapeSettings();
	compound_shape->AddShape(Vec3(0, 0, 0), Quat::sRotation(Vec3::sAxisX(), -0.25f * JPH_PI) * Quat::sRotation(Vec3::sAxisZ(), 0.25f * JPH_PI), sub_compound);
	compound_shape->AddShape(Vec3(0, -0.1f, 0), Quat::sRotation(Vec3::sAxisX(), 0.25f * JPH_PI) * Quat::sRotation(Vec3::sAxisZ(), -0.75f * JPH_PI), sub_compound);
	scene->AddBody(BodyCreationSettings(compound_shape, next_pos(), Quat::sRotation(Vec3::sAxisZ(), 0.5f * JPH_PI), EMotionType::Dynamic, Layers::MOVING));

	// Convex hull shape
	Array<Vec3> tetrahedron;
	tetrahedron.push_back(Vec3(-0.5f, 0, -0.5f));
	tetrahedron.push_back(Vec3(0, 0, 0.5f));
	tetrahedron.push_back(Vec3(0.5f, 0, -0.5f));
	tetrahedron.push_back(Vec3(0, -0.5f, 0));
	Ref<ConvexHullShapeSettings> convex_hull = new ConvexHullShapeSettings(tetrahedron, cDefaultConvexRadius, new PhysicsMaterialSimple("Convex Hull Material", next_color()));
	scene->AddBody(BodyCreationSettings(convex_hull, next_pos(), Quat::sIdentity(), EMotionType::Dynamic, Layers::MOVING));

	// Rotated convex hull
	scene->AddBody(BodyCreationSettings(new RotatedTranslatedShapeSettings(Vec3::sReplicate(0.5f), Quat::sRotation(Vec3::sAxisZ(), 0.25f * JPH_PI), convex_hull), next_pos(), Quat::sIdentity(), EMotionType::Dynamic, Layers::MOVING));

	// Mutable compound
	MutableCompoundShapeSettings *mutable_compound = new MutableCompoundShapeSettings();
	mutable_compound->AddShape(Vec3(0, 0.5f, 0), Quat::sRotation(Vec3::sAxisZ(), 0.5f * JPH_PI), new BoxShapeSettings(Vec3(0.5f, 0.1f, 0.2f), cDefaultConvexRadius, new PhysicsMaterialSimple("MutableCompound Box Material", next_color())));
	mutable_compound->AddShape(Vec3(0.5f, 0, 0), Quat::sRotation(Vec3::sAxisZ(), 0.5f * JPH_PI), new CapsuleShapeSettings(0.5f, 0.1f, new PhysicsMaterialSimple("MutableCompound Capsule Material", next_color())));
	mutable_compound->AddShape(Vec3(0, 0, 0.5f), Quat::sRotation(Vec3::sAxisX(), 0.5f * JPH_PI), new TaperedCapsuleShapeSettings(0.5f, 0.2f, 0.1f, new PhysicsMaterialSimple("MutableCompound Tapered Capsule Material", next_color())));
	scene->AddBody(BodyCreationSettings(mutable_compound, next_pos(), Quat::sRotation(Vec3::sAxisZ(), 0.5f * JPH_PI), EMotionType::Dynamic, Layers::MOVING));

	// Connect the first two dynamic bodies with a distance constraint
	DistanceConstraintSettings *dist_constraint = new DistanceConstraintSettings();
	dist_constraint->mSpace = EConstraintSpace::LocalToBodyCOM;
	scene->AddConstraint(dist_constraint, 3, 4);

	// Add soft body cube
	Ref<SoftBodySharedSettings> sb_cube_settings = SoftBodySharedSettings::sCreateCube(5, 0.2f);
	sb_cube_settings->mMaterials = { new PhysicsMaterialSimple("Soft Body Cube Material", next_color()) };
	SoftBodyCreationSettings sb_cube(sb_cube_settings, next_pos(), Quat::sIdentity(), Layers::MOVING);
	scene->AddSoftBody(sb_cube);

	// Add the same shape again to test sharing
	sb_cube.mPosition = next_pos();
	scene->AddSoftBody(sb_cube);

	// Add soft body sphere
	Ref<SoftBodySharedSettings> sb_sphere_settings = SoftBodyCreator::CreateSphere(0.5f);
	sb_sphere_settings->mMaterials = { new PhysicsMaterialSimple("Soft Body Sphere Material", next_color()) };
	SoftBodyCreationSettings sb_sphere(sb_sphere_settings, next_pos(), Quat::sIdentity(), Layers::MOVING);
	sb_sphere.mPressure = 2000.0f;
	scene->AddSoftBody(sb_sphere);

	return scene;
}

void LoadSaveSceneTest::Initialize()
{
#ifdef JPH_OBJECT_STREAM
	Ref<PhysicsScene> scene = sCreateScene();

	stringstream data;

	// Write scene
	if (!ObjectStreamOut::sWriteObject(data, ObjectStream::EStreamType::Text, *scene))
		FatalError("Failed to save scene");

	// Clear scene
	scene = nullptr;

	// Read scene back in
	if (!ObjectStreamIn::sReadObject(data, scene))
		FatalError("Failed to load scene");

	// Ensure that the soft body shared settings have been optimized (this is not saved to a text file)
	for (SoftBodyCreationSettings &soft_body : scene->GetSoftBodies())
		const_cast<SoftBodySharedSettings *>(soft_body.mSettings.GetPtr())->Optimize();

	// Instantiate scene
	scene->CreateBodies(mPhysicsSystem);
#endif // JPH_OBJECT_STREAM
}
