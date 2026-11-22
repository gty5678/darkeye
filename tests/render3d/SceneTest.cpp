#include "render3d/Animator.h"
#include "render3d/Camera.h"
#include "render3d/ModelAsset.h"
#include "render3d/Raycaster.h"
#include <QTest>
#include <limits>

using namespace darkeye::render3d;

class SceneTest final : public QObject {
    Q_OBJECT
private slots:
    void hierarchyAndDirtyPropagation()
    {
        Scene scene;
        const auto parent = scene.createNode();
        const auto child = scene.createNode(parent);
        const auto grandchild = scene.createNode(child);
        Transform first;
        first.translation = {3, 0, 0};
        first.rotation = QQuaternion::fromAxisAndAngle(0, 0, 1, 90);
        first.scale = {2, 2, 2};
        QVERIFY(scene.setTransform(parent, first));
        Transform local;
        local.translation = {1, 0, 0};
        QVERIFY(scene.setTransform(child, local));
        QVERIFY((scene.worldMatrix(grandchild).map(QVector3D()) - QVector3D(3, 2, 0)).length() < 1e-5f);
        first.translation.setX(5);
        QVERIFY(scene.setTransform(parent, first));
        QVERIFY((scene.worldMatrix(grandchild).map(QVector3D()) - QVector3D(5, 2, 0)).length() < 1e-5f);
        QVERIFY(!scene.setParent(parent, grandchild));
        QVERIFY(!scene.setParent(parent, parent));
        QVERIFY(scene.setParent(child, 0));
        QCOMPARE(scene.worldMatrix(grandchild).map(QVector3D()), QVector3D(1, 0, 0));
        QVERIFY(scene.removeNode(child));
        QVERIFY(!scene.node(grandchild));
        QCOMPARE(scene.size(), std::size_t(1));
        scene.clear();
        QVERIFY(scene.createNode() > grandchild);
        QVERIFY(!scene.createNode(999));
        Transform bad;
        bad.translation.setX(std::numeric_limits<float>::infinity());
        QVERIFY(!scene.setTransform(scene.createNode(), bad));
    }

    void sharedAssetAndVisibility()
    {
        auto mesh = std::make_shared<const MeshData>(coloredCube());
        auto material = std::make_shared<const Material>();
        ModelAsset asset;
        asset.nodes.push_back({"root", -1, {}, {{mesh, material}}});
        asset.nodes.push_back({"child", 0, {}, {{mesh, material}}});
        Scene scene;
        const auto first = asset.instantiate(scene);
        const auto second = asset.instantiate(scene);
        QCOMPARE(first.size(), std::size_t(2));
        QCOMPARE(second.size(), std::size_t(2));
        const auto items = scene.renderItems();
        QCOMPARE(items.size(), std::size_t(4));
        for (const auto &item : items)
            QCOMPARE(item.mesh.get(), mesh.get());
        QVERIFY(scene.setVisible(first[0], false));
        QCOMPARE(scene.renderItems().size(), std::size_t(2));
        asset.nodes[0].parent = 1;
        const auto before = scene.size();
        QVERIFY(asset.instantiate(scene).empty());
        QCOMPARE(scene.size(), before); // invalid asset is rejected atomically
    }

    void cameraProjectionAndRay()
    {
        Camera camera;
        camera.target = {2, 1, -3};
        camera.yaw = 25;
        camera.pitch = -15;
        const QSize viewport(801, 599);
        const auto center = camera.project(camera.target, viewport);
        QVERIFY(center.has_value());
        QVERIFY(QLineF(*center, QPointF(400.5, 299.5)).length() < 0.001);
        for (const auto &point : {QVector3D(2,1,-3), QVector3D(2.5f,1.5f,-3), QVector3D(1,0,-2)}) {
            const auto projected = camera.project(point, viewport);
            QVERIFY(projected.has_value());
            const auto ray = camera.ray(*projected, viewport);
            QVERIFY(ray.has_value());
            QVERIFY(QVector3D::crossProduct(point - ray->origin, ray->direction).length() < 0.001f);
        }
        QVERIFY(!camera.ray({}, {}).has_value());
        const auto oldDistance = camera.distance;
        camera.zoom(1);
        QVERIFY(camera.distance < oldDistance);
        camera.orbit(400, 1000);
        QCOMPARE(camera.pitch, 85.0f);
        QVERIFY(std::abs(camera.yaw) <= 180);
    }

    void inPlaceCameraTurnsWithoutMovingEye()
    {
        Camera camera;
        camera.rotationMode=Camera::RotationMode::InPlace;
        camera.target={2,0.1f,0};
        camera.distance=0.25f;
        const auto eye=camera.position();
        camera.yaw=15;
        camera.pitch=-10;
        QCOMPARE(camera.position(),eye);
        const auto forward=QQuaternion::fromEulerAngles(camera.pitch,camera.yaw,0).rotatedVector({0,0,-1});
        const QSize viewport(800,600);
        const auto projected=camera.project(eye+forward,viewport);
        QVERIFY(projected);
        QVERIFY(QLineF(*projected,QPointF(400,300)).length()<0.001);
        const auto ray=camera.ray(QPointF(400,300),viewport);
        QVERIFY(ray);
        QVERIFY(QVector3D::dotProduct(ray->direction,forward)>0.99999f);
    }

    void nearestRotatedBox()
    {
        Scene scene;
        auto mesh = std::make_shared<const MeshData>(coloredCube());
        const auto rear = scene.createNode();
        const auto front = scene.createNode();
        QVERIFY(scene.setMesh(rear, mesh));
        QVERIFY(scene.setMesh(front, mesh));
        Transform transform;
        transform.translation = {0, 0, 2};
        transform.rotation = QQuaternion::fromAxisAndAngle(0, 1, 0, 45);
        transform.scale = {0.5f, 1, 2};
        QVERIFY(scene.setTransform(front, transform));
        const auto hit = raycast(scene, {{0,0,10}, {0,0,-1}});
        QVERIFY(hit.has_value());
        QCOMPARE(hit->node, front);
        QVERIFY(hit->distance > 0 && hit->distance < 8);
        QVERIFY(!raycast(scene, {{10,0,10}, {0,0,-1}}).has_value());
        QVERIFY(scene.setVisible(front, false));
        QCOMPARE(raycast(scene, {{0,0,10}, {0,0,-1}})->node, rear);
        transform.scale = {0,0,0};
        QVERIFY(scene.setTransform(rear, transform));
        QVERIFY(!raycast(scene, {{0,0,10}, {0,0,-1}}).has_value());
    }

    void trianglePickingRejectsEmptyBounds()
    {
        Scene scene;
        auto mesh=std::make_shared<MeshData>();
        mesh->vertices={{{0,0,2},{}},{{2,0,2},{}},{{0,2,2},{}}};
        mesh->indices={0,1,2};
        const auto front=scene.createNode();
        QVERIFY(scene.setMesh(front,mesh));
        const auto rear=scene.createNode();
        QVERIFY(scene.setMesh(rear,std::make_shared<MeshData>(coloredCube())));
        Transform pose;
        pose.translation={1,1,0};
        pose.scale={4,4,1};
        QVERIFY(scene.setTransform(rear,pose));
        const Ray emptyCorner{{1.5f,1.5f,10},{0,0,-1}};
        QCOMPARE(raycast(scene,emptyCorner)->node,front);
        const auto precise=raycast(scene,emptyCorner,RaycastMode::Triangles);
        QVERIFY(precise);
        QCOMPARE(precise->node,rear);
        const Ray face{{0.25f,0.25f,10},{0,0,-1}};
        const auto surface=raycast(scene,face,RaycastMode::Triangles);
        QVERIFY(surface);
        QCOMPARE(surface->node,front);
        QVERIFY(std::abs(surface->distance-8)<0.0001f);
        QVERIFY(scene.setVisible(rear,false));
        QVERIFY(!raycast(scene,emptyCorner,RaycastMode::Triangles));
        // Double-sided geometry remains pickable from either side.
        QCOMPARE(raycast(scene,{{0.25f,0.25f,0},{0,0,1}},RaycastMode::Triangles)->node,front);
    }

    void animationInterruptionAndExactEndpoint()
    {
        Scene scene;
        const auto id = scene.createNode();
        Animator animator;
        Transform target;
        target.translation = {10,0,0};
        target.rotation = QQuaternion::fromEulerAngles(0,90,0);
        QVERIFY(animator.animateTo(scene, id, target, 0, 1));
        animator.advance(scene, 0.5);
        QCOMPARE(scene.node(id)->transform.translation, QVector3D(5,0,0));
        QVERIFY(animator.animateTo(scene, id, {}, 0.5, 0.5));
        QCOMPARE(scene.node(id)->transform.translation, QVector3D(5,0,0));
        animator.advance(scene, 1);
        QCOMPARE(scene.node(id)->transform.translation, QVector3D());
        QVERIFY(!animator.active());
        for (int i = 0; i < 100; ++i) {
            QVERIFY(animator.animateTo(scene, id, target, i, 1));
            animator.advance(scene, i + 0.1);
            QVERIFY(animator.animateTo(scene, id, {}, i + 0.1, 0.5));
            animator.advance(scene, i + 0.6);
            QCOMPARE(scene.node(id)->transform.translation, QVector3D());
        }
        QVERIFY(animator.animateTo(scene, id, target, 200, 0));
        QCOMPARE(scene.node(id)->transform.translation, target.translation);
        QVERIFY(animator.animateTo(scene, id, {}, 201, 1));
        QVERIFY(scene.removeNode(id));
        animator.advance(scene, 202);
        QVERIFY(!animator.active());
    }
};
QTEST_GUILESS_MAIN(SceneTest)
#include "SceneTest.moc"
