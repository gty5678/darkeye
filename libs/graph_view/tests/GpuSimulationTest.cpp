#include "GpuSimulation.h"
#include "PhysicsState.h"
#include "Simulation.h"
#include "ForceViewRhiWidget.h"
#include "ForcePerformanceOverlay.h"
#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QLabel>
#include <QMenu>
#include <QToolButton>
#include <QWheelEvent>
#include <QElapsedTimer>
#include <QOffscreenSurface>
#include <QTest>
#include <QSignalSpy>
#include <QHBoxLayout>
#include <QtWidgets/QRhiWidget>
#include <rhi/qrhi.h>
#include <rhi/qrhi_platform.h>
#include <cmath>
#include <array>
#include <algorithm>
#include <cstring>
#include <random>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <d3d11.h>
#include <d3d11sdklayers.h>
#include <wrl/client.h>
#endif

class GpuSimulationTest : public QObject {
    Q_OBJECT
    std::unique_ptr<QRhi> rhi;
    std::unique_ptr<QOffscreenSurface> surface;

    void fill(PhysicsState& s, int count, bool overlap = false)
    {
        std::vector<int> edges;
        for (int i = 1; i < count; ++i) {
            edges.push_back(i - 1);
            edges.push_back(i);
        }
        s.init(count, edges);
        std::mt19937 rng(42);
        std::uniform_real_distribution<float> position(-150.0f, 150.0f);
        for (int i = 0; i < count; ++i) {
            s.pos[2 * i] = overlap ? 0.0f : position(rng);
            s.pos[2 * i + 1] = overlap ? 0.0f : position(rng);
            s.mass[i] = 0.5f + (i % 7) * 0.25f;
        }
        s.syncDragPosFromPos();
    }

    bool step(GpuSimulation& gpu, PhysicsState& state, const GpuSimulation::Parameters& p,
              std::vector<float>& output)
    {
        QRhiCommandBuffer* cb = nullptr;
        if (rhi->beginOffscreenFrame(&cb) != QRhi::FrameOpSuccess) return false;
        const bool recorded = gpu.recordStep(cb, state, p);
        const auto status = rhi->endOffscreenFrame();
        // Offscreen completion synchronizes scheduled readbacks. Both the
        // harness and widget submit compute without an explicit finish().
        return recorded && status == QRhi::FrameOpSuccess && gpu.takeResult(output);
    }

    void cpuEngine(Simulation& sim, float theta = 0.0f)
    {
        sim.addForce("manybody", std::make_unique<ManyBodyForce>(10000.0f, 40000.0f, theta));
        sim.addForce("center", std::make_unique<CenterForce>(0.0f, 0.0f, 0.01f));
        sim.addForce("link", std::make_unique<LinkForce>(0.3f, 30.0f));
        sim.addForce("collision", std::make_unique<CollisionForce>(10.0f, 50.0f));
        sim.start();
    }

private slots:
    void cutoffBoundaryPreservesNearbyForce()
    {
        PhysicsState state;
        state.init(1500, {});
        for (int i = 0; i < state.nNodes; ++i) {
            state.pos[2 * i] = i == 0 ? 0.0f : i == 1 ? 190.0f : 210.0f + i * 0.001f;
            state.pos[2 * i + 1] = i < 2 ? 0.0f : (i % 17) * 0.001f;
        }
        GpuSimulation gpu;
        QVERIFY(gpu.initialize(rhi.get(), state));
        GpuSimulation::Parameters p;
        p.manyBody = 100.0f;
        p.center = p.link = p.collisionStrength = 0.0f;
        std::vector<float> output;
        QVERIFY(step(gpu, state, p, output));
        const float expected = -100.0f / (190.0f * 190.0f) * p.velocityDecay * p.dt;
        QVERIFY(std::abs(output[0] - expected) < 1e-7f);
        QVERIFY(std::abs(output[1]) < 1e-7f);
    }

    void topologyMutationReleasesDraggedNode()
    {
        ForceViewRhiWidget view;
        view.setSimulationBackendMode("cpu");
        view.setGraph(3, {}, {0, 0, 50, 0, 100, 0}, {"a", "b", "c"}, {}, {});
        view.pauseSimulation();
        view.m_physicsState->dragging[1] = 1;
        view.m_selectedIndex = 1;
        view.m_dragging = true;
        // Removing another node shifts the dragged node's index.
        view.remove_node_runtime("a");
        view.pauseSimulation();
        QCOMPARE(view.m_selectedIndex, -1);
        QVERIFY(!view.m_dragging);
        QVERIFY(std::all_of(view.m_physicsState->dragging.begin(),
                            view.m_physicsState->dragging.end(),
                            [](uint8_t value) { return value == 0; }));
    }

    void backgroundGpuSimulation_data()
    {
        QTest::addColumn<bool>("minimized");
        QTest::newRow("hidden-page") << false;
        QTest::newRow("minimized-window") << true;
    }

    void backgroundGpuSimulation()
    {
        QFETCH(bool, minimized);
        ForceViewRhiWidget view;
        view.resize(600, 400);
        view.setSimulationBackendMode("gpu");
        view.setManyBodyStrength(0);
        view.setCenterStrength(0);
        view.setLinkStrength(0);
        view.setCollisionStrength(0);
        view.setGraph(2, {}, {-100, 0, 100, 0}, {"a", "b"}, {"a", "b"}, {4, 4});
        auto *canvas = view.findChild<QRhiWidget*>();
        QVERIFY(canvas);
        QSignalSpy frames(canvas, &QRhiWidget::frameSubmitted);
        QSignalSpy stopped(&view, &ForceViewRhiWidget::simulationStopped);
        view.show();
        QTRY_VERIFY_WITH_TIMEOUT(view.m_gpuSimulation && view.m_gpuSimulation->submittedSteps() > 0, 5000);
        if (minimized) view.showMinimized();
        else view.hide();
        QTest::qWait(100); // Drain any already requested paints.
        const int frameCount = frames.size();
        const int ticks = view.m_simulation->tickCount();
        QTRY_VERIFY_WITH_TIMEOUT(view.m_simulation->tickCount() > ticks + 3, 3000);
        QCOMPARE(frames.size(), frameCount);
        QCOMPARE(view.activeSimulationBackend(), QStringLiteral("gpu"));

        view.pauseSimulation();
        const int pausedTicks = view.m_simulation->tickCount();
        QTest::qWait(100);
        QCOMPARE(view.m_simulation->tickCount(), pausedTicks);
        view.resumeSimulation();
        QTRY_VERIFY_WITH_TIMEOUT(view.m_simulation->tickCount() > pausedTicks, 3000);
        stopped.clear();
        QTRY_VERIFY_WITH_TIMEOUT(!view.m_simulation->isActive(), 10000);
        QTRY_VERIFY_WITH_TIMEOUT(!view.m_gpuSimulation->hasPendingStep(), 3000);
        QCOMPARE(stopped.size(), 1);
        const int settledTicks = view.m_simulation->tickCount();
        QTest::qWait(100);
        QCOMPARE(view.m_simulation->tickCount(), settledTicks);
        view.showNormal();
        QTRY_VERIFY_WITH_TIMEOUT(frames.size() > frameCount, 3000);
        QCOMPARE(view.m_simulation->tickCount(), settledTicks);
    }

    void paintingDoesNotAdvanceGpuSimulation()
    {
        ForceViewRhiWidget view;
        view.resize(600, 400);
        view.setSimulationBackendMode("gpu");
        view.setGraph(2, {}, {-100, 0, 100, 0}, {"a", "b"}, {"a", "b"}, {4, 4});
        view.show();
        QTRY_VERIFY_WITH_TIMEOUT(view.m_gpuSimulation && view.m_gpuSimulation->submittedSteps() > 0, 5000);
        view.m_gpuTimer.stop();
        const int ticks = view.m_simulation->tickCount();
        auto *canvas = view.findChild<QRhiWidget*>();
        QVERIFY(canvas);
        QSignalSpy frames(canvas, &QRhiWidget::frameSubmitted);
        for (int i = 0; i < 3; ++i) {
            const int count = frames.size();
            canvas->update();
            QTRY_VERIFY_WITH_TIMEOUT(frames.size() > count, 3000);
            QCOMPARE(view.m_simulation->tickCount(), ticks);
        }
        view.pauseSimulation();
    }

    void rightDragDoesNotOpenPerformanceMenu()
    {
        ForceViewRhiWidget view;
        view.resize(640, 520);
        view.show();
        QTest::qWait(100);
        auto *canvas = view.findChild<QRhiWidget*>();
        QVERIFY(canvas);
        const QPointF start(20, 20), moved(80, 60);
        const auto mouse = [canvas](QEvent::Type type, const QPointF &position) {
            const bool moving = type == QEvent::MouseMove;
            QMouseEvent event(type, position, canvas->mapToGlobal(position.toPoint()),
                moving ? Qt::NoButton : Qt::RightButton,
                type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::RightButton,
                Qt::NoModifier);
            QApplication::sendEvent(canvas, &event);
        };
        const auto context = [canvas](QContextMenuEvent::Reason reason) {
            QContextMenuEvent event(reason, QPoint(20, 20), canvas->mapToGlobal(QPoint(20, 20)));
            QApplication::sendEvent(canvas, &event);
            QVERIFY(event.isAccepted());
        };
        // Empty view and populated view both suppress a moved right gesture.
        for (const bool populated : {false, true}) {
            if (populated) {
                view.setGraph(1, {}, {0.0f, 0.0f}, {"node"}, {"node"}, {7.0f});
                view.pauseSimulation();
            }
            mouse(QEvent::MouseButtonPress, start);
            mouse(QEvent::MouseMove, moved);
            mouse(QEvent::MouseMove, start); // Returning does not turn it into a click.
            mouse(QEvent::MouseButtonRelease, start);
            context(QContextMenuEvent::Mouse);
            QVERIFY(!view.findChild<QMenu*>("forceViewContextMenu"));
            // Also detect displacement when no intervening move was delivered.
            mouse(QEvent::MouseButtonPress, start);
            mouse(QEvent::MouseButtonRelease, moved);
            context(QContextMenuEvent::Mouse);
            QVERIFY(!view.findChild<QMenu*>("forceViewContextMenu"));
        }
        // A subsequent stationary click resets suppression and opens the menu.
        mouse(QEvent::MouseButtonPress, start);
        mouse(QEvent::MouseButtonRelease, start);
        context(QContextMenuEvent::Mouse);
        auto *menu = view.findChild<QMenu*>("forceViewContextMenu");
        QVERIFY(menu && menu->isVisible());
        menu->hide();
        // Keyboard access is independent of the last mouse gesture.
        mouse(QEvent::MouseButtonPress, start);
        mouse(QEvent::MouseMove, moved);
        mouse(QEvent::MouseButtonRelease, moved);
        context(QContextMenuEvent::Mouse);
        QVERIFY(!menu->isVisible());
        context(QContextMenuEvent::Keyboard);
        QVERIFY(menu->isVisible());
        menu->hide();
    }

    void performanceOverlayIsLocalAndKeepsCanvasGeometry()
    {
        ForceViewRhiWidget first, second;
        first.resize(640, 520);
        second.resize(640, 520);
        first.show();
        second.show();
        QTest::qWait(100);
        auto* canvas = first.findChild<QRhiWidget*>();
        QVERIFY(canvas);
        // First right-click creates statistics on demand in production too.
        QVERIFY(!first.findChild<ForcePerformanceOverlay*>());
        QVERIFY(!second.findChild<ForcePerformanceOverlay*>());
        QVERIFY(!first.isPerformanceOverlayVisible());
        QVERIFY(!second.isPerformanceOverlayVisible());
        const QSize canvasSize = canvas->size();
        const float zoom = first.getZoom();
        const QPointF pan(first.getPanX(), first.getPanY());
        QContextMenuEvent context(QContextMenuEvent::Mouse, QPoint(20, 20), canvas->mapToGlobal(QPoint(20, 20)));
        QApplication::sendEvent(canvas, &context);
        auto* menu = first.findChild<QMenu*>("forceViewContextMenu");
        auto* action = first.findChild<QAction*>("togglePerformanceOverlayAction");
        auto* overlay = first.findChild<ForcePerformanceOverlay*>();
        QVERIFY(overlay);
        QVERIFY(!second.findChild<ForcePerformanceOverlay*>());
        QVERIFY(menu && menu->isVisible());
        QVERIFY(action && !action->isChecked());
        action->trigger();
        menu->hide();
        QVERIFY(first.isPerformanceOverlayVisible());
        QVERIFY(!second.isPerformanceOverlayVisible());
        QCOMPARE(overlay->pos(), QPoint(12, 12));
        QCOMPARE(first.childAt(QPoint(13, 13)), overlay);
        QCOMPARE(canvas->size(), canvasSize);

        auto* profile = overlay->findChild<QCheckBox*>("gpuProfilingCheckBox");
        QVERIFY(profile);
        QVERIFY(profile->isChecked());
        QVERIFY(first.m_gpuProfiling);
        QVERIFY(!second.m_gpuProfiling);
        profile->setChecked(false);
        QVERIFY(!first.m_gpuProfiling);
        first.setPerformanceOverlayVisible(true);
        QVERIFY(!first.m_gpuProfiling); // Preserve a manual opt-out while shown.
        first.setGpuProfilingEnabled(true);
        QVERIFY(profile->isChecked());
        first.hide();
        QVERIFY(!first.m_gpuProfiling);
        QVERIFY(!profile->isChecked());
        first.show();
        QVERIFY(first.m_gpuProfiling);
        QVERIFY(profile->isChecked());
        auto* mode = overlay->findChild<QComboBox*>("performanceBackendMode");
        QVERIFY(mode);
        mode->setCurrentIndex(mode->findData("cpu"));
        QCOMPARE(first.simulationBackendMode(), QStringLiteral("cpu"));
        QCOMPARE(second.simulationBackendMode(), QStringLiteral("auto"));
        // Reopening displays the latest hidden telemetry immediately, without
        // racing real render reports against the overlay's refresh timer.
        first.setPerformanceOverlayVisible(false);
        first.tickTimeUpdated(12.5f);
        second.tickTimeUpdated(99.0f);
        first.performanceDetailsUpdated({{"geometryCpuMs", 0.25}, {"dispatches", 48}, {"gpuInstances", true}});
        first.setPerformanceOverlayVisible(true);
        QCOMPARE(overlay->findChild<QLabel*>("performance_tick")->text(), QStringLiteral("12.50 ms"));
        QCOMPARE(overlay->findChild<QLabel*>("performance_dispatches")->text(), QStringLiteral("48"));
        QCOMPARE(canvas->size(), canvasSize);

        QWheelEvent wheel(QPointF(1, 1), overlay->mapToGlobal(QPoint(1, 1)), QPoint(), QPoint(0, 120),
                          Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(overlay, &wheel);
        QTest::mousePress(overlay, Qt::LeftButton, Qt::NoModifier, QPoint(1, 1));
        QTest::mouseMove(overlay, QPoint(20, 20));
        QTest::mouseRelease(overlay, Qt::LeftButton, Qt::NoModifier, QPoint(20, 20));
        QCOMPARE(first.getZoom(), zoom);
        QCOMPARE(QPointF(first.getPanX(), first.getPanY()), pan);
        QVERIFY(first.grab().save("performance-overlay.png"));
        first.resize(200, 150);
        QTest::qWait(50);
        QVERIFY(first.rect().contains(overlay->geometry()));
        auto* close = overlay->findChild<QToolButton*>("closePerformanceOverlay");
        QVERIFY(close);
        QTest::mouseClick(close, Qt::LeftButton);
        QVERIFY(!first.isPerformanceOverlayVisible());
        QVERIFY(!action->isChecked());
        QVERIFY(!first.m_gpuProfiling);
        QVERIFY(!profile->isChecked());
        action->trigger();
        QVERIFY(first.isPerformanceOverlayVisible());
        QVERIFY(first.m_gpuProfiling);
        QVERIFY(profile->isChecked());
        QVERIFY(!second.isPerformanceOverlayVisible());
        QVERIFY(!second.m_gpuProfiling);
        action->trigger();
        QVERIFY(!first.m_gpuProfiling);
        QVERIFY(!profile->isChecked());
    }

    void initTestCase()
    {
#if defined(Q_OS_WIN)
        QRhiD3D11InitParams params;
        rhi.reset(QRhi::create(QRhi::D3D11, &params));
#elif defined(Q_OS_MACOS)
        QRhiMetalInitParams params;
        rhi.reset(QRhi::create(QRhi::Metal, &params));
#else
        surface.reset(QRhiGles2InitParams::newFallbackSurface());
        QRhiGles2InitParams params;
        params.fallbackSurface = surface.get();
        rhi.reset(QRhi::create(QRhi::OpenGLES2, &params));
#endif
        QVERIFY2(rhi, "An actual graphics device is required for GPU layout tests");
        QVERIFY(rhi->isFeatureSupported(QRhi::Compute));
        qInfo() << "GPU layout device:" << rhi->backendName() << rhi->driverInfo();
    }

    void exactMatchesCpu_data()
    {
        QTest::addColumn<int>("count");
        QTest::addColumn<bool>("overlap");
        QTest::newRow("single") << 1 << false;
        QTest::newRow("pair") << 2 << false;
        QTest::newRow("partial-workgroup") << 129 << false;
        QTest::newRow("beyond-cpu-bh-threshold") << 1501 << false;
        QTest::newRow("duplicate-keys") << 137 << true;
    }

    void exactMatchesCpu()
    {
        QFETCH(int, count);
        QFETCH(bool, overlap);
        PhysicsState state, cpu;
        fill(state, count, overlap);
        fill(cpu, count, overlap);
        GpuSimulation gpu;
        QString error;
        QVERIFY2(gpu.initialize(rhi.get(), state, &error), qPrintable(error));
        Simulation sim(&cpu);
        cpuEngine(sim);
        GpuSimulation::Parameters p;
        p.theta = 0.0f;
        std::vector<float> output;
        // A single exact step isolates GPU formula correctness from divergent
        // floating point trajectories and the CPU's degenerate quadtree policy.
        sim.tick();
        QVERIFY2(step(gpu, state, p, output), qPrintable(gpu.errorString()));
        for (int i = 0; i < count; ++i) {
            for (int axis = 0; axis < 2; ++axis) {
                const float expected = cpu.pos[2 * i + axis];
                const float actual = output[4 * i + axis];
                QVERIFY2(std::abs(expected - actual) < 0.015f,
                         qPrintable(QString("node %1 axis %2: CPU %3 GPU %4").arg(i).arg(axis).arg(expected).arg(actual)));
            }
        }
    }

    void residentStateAndDragTargets()
    {
        PhysicsState state;
        fill(state, 65);
        GpuSimulation gpu;
        QVERIFY(gpu.initialize(rhi.get(), state));
        GpuSimulation::Parameters p;
        std::vector<float> first, second;
        QVERIFY(step(gpu, state, p, first));
        // CPU positions deliberately remain stale: the next step must consume
        // resident GPU state, while a new drag target still overrides it.
        state.dragging[4] = 1;
        state.setDragPos(4, 12.0f, -17.0f);
        QVERIFY(step(gpu, state, p, second));
        QCOMPARE(second[16], 12.0f);
        QCOMPARE(second[17], -17.0f);
        QVERIFY(first[0] != second[0] || first[1] != second[1]);
    }

    void optimizedMatchesReference_data()
    {
        QTest::addColumn<int>("count");
        for (int n : {1, 127, 128, 129, 256, 257, 10000, 100000})
            QTest::newRow(qPrintable(QString::number(n))) << n;
    }
    void optimizedMatchesReference()
    {
        QFETCH(int, count);
        PhysicsState state;
        fill(state, count);
        const float scale = (std::sqrt(float(count)) * 25.0f + 50.0f) / 150.0f;
        for (float& x : state.pos) x *= scale;
        state.syncDragPosFromPos();
        GpuSimulation optimized, reference;
        QVERIFY(optimized.initialize(rhi.get(), state));
        QVERIFY(reference.initialize(rhi.get(), state, nullptr, false));
        std::vector<float> actual, expected;
        GpuSimulation::Parameters p;
        QVERIFY(step(reference, state, p, expected));
        QVERIFY(step(optimized, state, p, actual));
        QCOMPARE(actual.size(), expected.size());
        for (size_t i = 0; i < actual.size(); ++i)
            QVERIFY2(std::abs(actual[i] - expected[i]) < 0.001f, qPrintable(QString("index %1 actual %2 reference %3").arg(i).arg(actual[i]).arg(expected[i])));
        if (count == 100000) QCOMPARE(optimized.timings().dispatches, 48);
    }

    void stageProfiling()
    {
        PhysicsState state;
        fill(state, 1024);
        GpuSimulation gpu;
        QVERIFY(gpu.initialize(rhi.get(), state));
        gpu.setProfilingEnabled(true);
        std::vector<float> output;
        QVERIFY(step(gpu, state, {}, output));
        QVERIFY(step(gpu, state, {}, output));
        const auto t = gpu.timings();
        QVERIFY(t.cpuRecordMs > 0);
        if (rhi->backend() == QRhi::D3D11) {
            QVERIFY(t.gpuSortMs >= 0);
            QVERIFY(t.gpuForceMs >= 0);
        }
    }

    void profilingWithD3D11Validation()
    {
#ifdef Q_OS_WIN
        QRhiD3D11InitParams params;
        params.enableDebugLayer = true;
        std::unique_ptr<QRhi> debugRhi(QRhi::create(QRhi::D3D11, &params, QRhi::EnableTimestamps));
        QVERIFY(debugRhi);
        const auto* handles = static_cast<const QRhiD3D11NativeHandles*>(debugRhi->nativeHandles());
        auto* device = static_cast<ID3D11Device*>(handles->dev);
        Microsoft::WRL::ComPtr<ID3D11InfoQueue> messages;
        if (FAILED(device->QueryInterface(IID_PPV_ARGS(&messages))))
            QSKIP("D3D11 debug layer is not installed");
        messages->ClearStoredMessages();
        PhysicsState state;
        fill(state, 400);
        GpuSimulation gpu;
        QVERIFY(gpu.initialize(debugRhi.get(), state));
        for (int frame = 0; frame < 12; ++frame) {
            gpu.setProfilingEnabled(frame % 3 != 0);
            QRhiCommandBuffer* cb = nullptr;
            QCOMPARE(debugRhi->beginOffscreenFrame(&cb), QRhi::FrameOpSuccess);
            QVERIFY(gpu.recordSteps(cb, state, {{}, {}}, true));
            QCOMPARE(debugRhi->endOffscreenFrame(), QRhi::FrameOpSuccess);
            std::vector<float> output;
            QVERIFY(gpu.takeResult(output));
        }
        QStringList errors;
        for (UINT64 i = 0; i < messages->GetNumStoredMessages(); ++i) {
            SIZE_T size = 0;
            messages->GetMessage(i, nullptr, &size);
            std::vector<char> storage(size);
            auto* message = reinterpret_cast<D3D11_MESSAGE*>(storage.data());
            if (SUCCEEDED(messages->GetMessage(i, message, &size))
                && message->Severity <= D3D11_MESSAGE_SEVERITY_WARNING)
                errors << QString::fromUtf8(message->pDescription);
        }
        QVERIFY2(errors.isEmpty(), qPrintable(errors.join('\n')));
#else
        QSKIP("D3D11 validation is Windows-only");
#endif
    }

    void tracePerformanceOverlayExceptions()
    {
#ifdef Q_OS_WIN
        if (!qEnvironmentVariableIsSet("DARKEYE_TRACE_OVERLAY_EXCEPTIONS")) QSKIP("Optional first-chance exception trace");
        struct Trace {
            DWORD thread = GetCurrentThreadId();
            int count = 0;
            void* frames[32][24] = {};
            USHORT sizes[32] = {};
        } capture;
        static Trace* trace = nullptr;
        trace = &capture;
        auto handler = AddVectoredExceptionHandler(1, [](EXCEPTION_POINTERS* exception) -> LONG {
            if (trace && GetCurrentThreadId() == trace->thread
                && exception->ExceptionRecord->ExceptionCode == 0xe06d7363 && trace->count < 32) {
                const int index = trace->count++;
                trace->sizes[index] = CaptureStackBackTrace(0, 24, trace->frames[index], nullptr);
            }
            return EXCEPTION_CONTINUE_SEARCH;
        });
        QVERIFY(handler);
        int beforeOverlay = 0;
        {
            ForceViewRhiWidget view;
            view.resize(640, 520);
            view.setSimulationBackendMode("gpu");
            view.setGraph(2, {0, 1}, {-20, 0, 20, 0}, {"a", "b"}, {"a", "b"}, {4, 4});
            view.show();
            QTest::qWait(100);
            beforeOverlay = capture.count;
            for (int i = 0; i < 3; ++i) {
                view.setPerformanceOverlayVisible(true);
                QTest::qWait(150);
                view.setPerformanceOverlayVisible(false);
                QTest::qWait(50);
            }
            qInfo() << "C++ exceptions before overlay:" << beforeOverlay << "after:" << capture.count;
        }
        RemoveVectoredExceptionHandler(handler);
        trace = nullptr;
        for (int i = 0; i < capture.count; ++i) {
            QStringList modules;
            for (int j = 0; j < capture.sizes[i]; ++j) {
                HMODULE module = nullptr;
                wchar_t path[MAX_PATH] = {};
                if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                     reinterpret_cast<LPCWSTR>(capture.frames[i][j]), &module)
                    && GetModuleFileNameW(module, path, MAX_PATH))
                    modules << QString::fromWCharArray(path).section('\\', -1);
            }
            qInfo() << "C++ first-chance stack modules:" << modules.join(" -> ");
        }
        QCOMPARE(capture.count, beforeOverlay);
#else
        QSKIP("Windows-only first-chance trace");
#endif
    }

    void computeContinuesWhileSnapshotIsPending()
    {
        PhysicsState state;
        fill(state, 129);
        GpuSimulation gpu, reference;
        QVERIFY(gpu.initialize(rhi.get(), state));
        QVERIFY(reference.initialize(rhi.get(), state));
        QRhiCommandBuffer* cb = nullptr;
        QCOMPARE(rhi->beginOffscreenFrame(&cb), QRhi::FrameOpSuccess);
        QVERIFY(gpu.recordSteps(cb, state, {{}}, true));
        QVERIFY(gpu.hasPendingStep());
        QVERIFY(gpu.recordSteps(cb, state, {{}}, false));
        QCOMPARE(gpu.submittedSteps(), 2);
        QCOMPARE(rhi->endOffscreenFrame(), QRhi::FrameOpSuccess);
        std::vector<float> first, latest, expected;
        QVERIFY(gpu.takeResult(first));
        QCOMPARE(gpu.snapshotSteps(), 1);
        QCOMPARE(rhi->beginOffscreenFrame(&cb), QRhi::FrameOpSuccess);
        QVERIFY(gpu.recordSnapshot(cb));
        QCOMPARE(rhi->endOffscreenFrame(), QRhi::FrameOpSuccess);
        QVERIFY(gpu.takeResult(latest));
        QCOMPARE(gpu.snapshotSteps(), 2);
        QVERIFY(step(reference, state, {}, expected));
        QVERIFY(step(reference, state, {}, expected));
        for (size_t i = 0; i < latest.size(); ++i) QVERIFY(std::abs(latest[i] - expected[i]) < 1e-5f);
    }

    void instancesMatchCpuRendering()
    {
        QWidget root;
        auto* layout = new QHBoxLayout(&root);
        auto* cpu = new ForceViewRhiWidget(&root);
        auto* gpu = new ForceViewRhiWidget(&root);
        for (auto* view : {cpu, gpu}) {
            layout->addWidget(view);
            view->setMinimumSize(400, 350);
            view->setManyBodyStrength(0); view->setCenterStrength(0);
            view->setLinkStrength(0); view->setCollisionStrength(0);
            view->setTextThresholdFactor(100);
            view->setGraph(3, {0, 1, 1, 2}, {-30, -10, 0, 20, 35, -5},
                {"a", "b", "c"}, {"a", "b", "c"}, {4, 6, 5}, {Qt::red, Qt::green, Qt::blue});
        }
        cpu->setSimulationBackendMode("cpu");
        gpu->setSimulationBackendMode("gpu");
        QSignalSpy details(gpu, &ForceViewRhiWidget::performanceDetailsUpdated);
        root.resize(830, 400); root.show();
        QTest::mouseMove(&root, QPoint(1, 1));
        QTRY_VERIFY_WITH_TIMEOUT(!details.isEmpty(), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(gpu->activeSimulationBackend(), QStringLiteral("gpu"), 5000);
        QTest::qWait(150);
        cpu->fitViewToContent(); gpu->fitViewToContent();
        auto* a = cpu->findChild<QRhiWidget*>();
        auto* b = gpu->findChild<QRhiWidget*>();
        QVERIFY(a && b);
        const QImage expected = a->grabFramebuffer().convertToFormat(QImage::Format_RGBA8888);
        const QImage actual = b->grabFramebuffer().convertToFormat(QImage::Format_RGBA8888);
        QCOMPARE(actual.size(), expected.size());
        double error = 0;
        int colored = 0;
        for (int y = 0; y < actual.height(); ++y)
            for (int x = 0; x < actual.width() * 4; ++x) {
                error += std::abs(int(actual.constScanLine(y)[x]) - int(expected.constScanLine(y)[x]));
                if (x % 4 == 0 && std::abs(int(actual.constScanLine(y)[x]) - int(actual.constScanLine(y)[x + 1])) > 32) ++colored;
            }
        error /= actual.width() * actual.height() * 4;
        QVERIFY2(error < 1.0, qPrintable(QString("Mean image difference %1").arg(error)));
        QVERIFY(colored > 100);
        QVERIFY(details.last()[0].toMap().value("gpuInstances").toBool());
        cpu->setTextThresholdFactor(0.1f);
        gpu->setTextThresholdFactor(0.1f);
        QTest::qWait(300);
        const QImage withTextExpected = a->grabFramebuffer().convertToFormat(QImage::Format_RGBA8888);
        const QImage withTextActual = b->grabFramebuffer().convertToFormat(QImage::Format_RGBA8888);
        double textError = 0;
        for (int y = 0; y < withTextActual.height(); ++y)
            for (int x = 0; x < withTextActual.width() * 4; ++x)
                textError += std::abs(int(withTextActual.constScanLine(y)[x]) - int(withTextExpected.constScanLine(y)[x]));
        textError /= withTextActual.width() * withTextActual.height() * 4;
        QVERIFY2(textError < 1.0, qPrintable(QString("CPU/GPU text image difference %1").arg(textError)));
        QVERIFY(details.last()[0].toMap().value("gpuText").toBool());
        cpu->update_node_runtime("a", {}, 4, Qt::yellow);
        gpu->update_node_runtime("a", {}, 4, Qt::yellow);
        QTest::qWait(50);
        const QImage recolored = b->grabFramebuffer().convertToFormat(QImage::Format_RGBA8888);
        int yellow = 0;
        for (int y = 0; y < recolored.height(); ++y) for (int x = 0; x < recolored.width(); ++x) {
            const auto pixel = recolored.pixelColor(x, y);
            if (pixel.red() > 200 && pixel.green() > 200 && pixel.blue() < 80) ++yellow;
        }
        QVERIFY(yellow > 100);
        // CPU rendering is the visual reference, including overlap ordering,
        // neighbor colors, hover enlargement, text, and the retained fade-out.
        cpu->m_hoverStep = gpu->m_hoverStep = 0;
        quint64 cachedRevision = 0;
        for (const float amount : {0.25f, 0.75f, 1.0f, 0.5f, 0.0f, 0.6f, 0.9f}) {
            for (auto* view : {cpu, gpu}) {
                if (amount == 0.25f) {
                    view->m_hoverIndex = 0;
                    view->updateNeighborMaskForHover(0);
                } else if (amount == 0.5f) {
                    QEvent leave(QEvent::Leave);
                    view->leaveEvent(&leave);
                } else if (amount == 0.6f) {
                    view->m_hoverIndex = 2;
                    view->updateNeighborMaskForHover(2);
                } else if (amount == 0.9f) {
                    view->setNeighborDepth(2);
                }
                view->m_hoverGlobal = amount;
                view->update();
            }
            QTest::qWait(30);
            const QImage reference = a->grabFramebuffer().convertToFormat(QImage::Format_RGBA8888);
            const QImage hovered = b->grabFramebuffer().convertToFormat(QImage::Format_RGBA8888);
            double difference = 0;
            for (int y = 0; y < hovered.height(); ++y)
                for (int x = 0; x < hovered.width() * 4; ++x)
                    difference += std::abs(int(hovered.constScanLine(y)[x]) - int(reference.constScanLine(y)[x]));
            difference /= hovered.width() * hovered.height() * 4;
            QVERIFY2(difference < 1.0, qPrintable(QString("Hover %1 CPU/GPU image difference %2").arg(amount).arg(difference)));
            if (amount == 0.25f) cachedRevision = gpu->m_gpuDescriptionRevision;
            else if (amount == 0.75f || amount == 1.0f || amount == 0.5f)
                QCOMPARE(gpu->m_gpuDescriptionRevision, cachedRevision);
            else if (amount == 0.6f || amount == 0.9f) {
                QVERIFY(gpu->m_gpuDescriptionRevision > cachedRevision);
                cachedRevision = gpu->m_gpuDescriptionRevision;
            }
            if (amount == 0.75f) QVERIFY(hovered.save("gpu-hover-overlay.png"));
        }
        // Hover palette changes require only uniforms, not new descriptions.
        const auto paletteRevision = gpu->m_gpuDescriptionRevision;
        for (auto* view : {cpu, gpu}) {
            view->setHoverColor(Qt::cyan); view->setDimColor(Qt::darkGray); view->setEdgeDimColor(Qt::gray);
        }
        const QImage paletteExpected = a->grabFramebuffer();
        const QImage paletteActual = b->grabFramebuffer();
        double paletteError = 0;
        for (int y = 0; y < paletteActual.height(); ++y) for (int x = 0; x < paletteActual.width(); ++x) {
            const QColor expectedColor = paletteExpected.pixelColor(x, y), actualColor = paletteActual.pixelColor(x, y);
            paletteError += std::abs(expectedColor.red() - actualColor.red()) + std::abs(expectedColor.green() - actualColor.green())
                + std::abs(expectedColor.blue() - actualColor.blue());
        }
        QVERIFY(paletteError / (paletteActual.width() * paletteActual.height() * 3) < 1.0);
        QCOMPARE(gpu->m_gpuDescriptionRevision, paletteRevision);
        const auto beforePause = details.size();
        gpu->pauseSimulation();
        QTest::qWait(300);
        b->grabFramebuffer();
        QVERIFY(details.size() > beforePause);
        QVERIFY(details.last()[0].toMap().value("gpuInstances").toBool());
    }

    void textUsesLatestResidentPosition()
    {
        PhysicsState state;
        state.init(1, {}); state.pos = {100, 20}; state.syncDragPosFromPos();
        GpuSimulation gpu;
        QVERIFY(gpu.initialize(rhi.get(), state));
        GpuGraphGeometry geometry;
        GpuGraphGeometry::Instance item;
        item.shape[0] = 4; item.shape[2] = 2; item.color[0] = item.color[3] = 1;
        const float viewport[] = {-1000, -1000, 1000, 1000};
        const float vertices[] = {
            0.35f, 0.7f, 0, 0, 1, 1, 1, 1, 1, 0, 1.23f, 2.2f,
            10.35f, 12.7f, 0, 0, 1, 1, 1, 1, 1, 0, 1.23f, 2.2f};
        const float screen[] = {1.75f, 1.5f, 2.3f, -4.2f, 500.5f, 350.5f};
        QRhiCommandBuffer* cb = nullptr;
        QCOMPARE(rhi->beginOffscreenFrame(&cb), QRhi::FrameOpSuccess);
        QVERIFY(gpu.recordStep(cb, state, {}));
        QVERIFY(geometry.record(rhi.get(), cb, gpu.stateBuffer(), {item}, viewport, 1));
        QVERIFY(geometry.recordText(cb, gpu.stateBuffer(), vertices, 2, screen));
        QRhiReadbackResult read;
        auto* updates = rhi->nextResourceUpdateBatch();
        updates->readBackBuffer(geometry.textBuffer(), 0, 96, &read); cb->resourceUpdate(updates);
        QCOMPARE(rhi->endOffscreenFrame(), QRhi::FrameOpSuccess);
        std::vector<float> output; QVERIFY(gpu.takeResult(output));
        QCOMPARE(read.data.size(), qsizetype(96));
        float text[24]; std::memcpy(text, read.data.constData(), 96);
        const float anchorX = (std::floor((output[0] + 1.23f - screen[2]) * screen[0] + screen[4] + 0.5f) - screen[4]) / screen[0] + screen[2];
        const float anchorY = (std::floor((output[1] + 2.2f - screen[3]) * screen[1] + screen[5] + 0.5f) - screen[5]) / screen[1] + screen[3];
        QVERIFY(std::abs(text[0] - (anchorX + 0.35f)) < 1e-4f);
        QVERIFY(std::abs(text[1] - (anchorY + 0.7f)) < 1e-4f);
        QVERIFY(std::abs(text[12] - text[0] - 10.0f) < 1e-4f);
        QVERIFY(std::abs(text[13] - text[1] - 12.0f) < 1e-4f);
    }

    void hoverShaderBlendsColorsWithoutDescriptionUpload()
    {
        PhysicsState state;
        state.init(2, {0, 1});
        state.pos = {-20, 0, 20, 0}; state.syncDragPosFromPos();
        GpuSimulation gpu;
        QVERIFY(gpu.initialize(rhi.get(), state));
        GpuGraphGeometry geometry;
        std::vector<GpuGraphGeometry::Instance> items(5);
        for (int i = 0; i < 5; ++i) {
            auto& item = items[i];
            item.nodes[0] = 0; item.nodes[1] = 1;
            item.shape[0] = 4; item.shape[1] = 6;
            item.shape[2] = i < 2 ? 1 : 2;
            item.color[0] = 0.2f; item.color[1] = 0.4f; item.color[2] = 0.6f; item.color[3] = 0.8f;
        }
        items[1].nodes[2] = items[3].nodes[2] = GpuGraphGeometry::Highlighted;
        items[4].nodes[2] = GpuGraphGeometry::Highlighted | GpuGraphGeometry::Hovered;
        GpuGraphGeometry::HoverStyle style;
        style.radiusScale = 1.1f;
        const float dim[] = {0.1f, 0.2f, 0.3f, 0.4f};
        const float edge[] = {0.05f, 0.1f, 0.15f, 0.2f};
        const float highlight[] = {1, 0, 0.5f, 1};
        std::copy_n(dim, 4, style.dimColor);
        std::copy_n(edge, 4, style.edgeDimColor);
        std::copy_n(highlight, 4, style.highlightColor);
        const float viewport[] = {-100, -100, 100, 100};
        for (const float amount : {0.0f, 0.25f, 1.0f, 0.5f, 0.0f}) {
            style.amount = amount;
            QRhiCommandBuffer* cb = nullptr;
            QCOMPARE(rhi->beginOffscreenFrame(&cb), QRhi::FrameOpSuccess);
            gpu.recordInitialState(cb);
            QVERIFY(geometry.record(rhi.get(), cb, gpu.stateBuffer(), items, viewport, 1, &style));
            QRhiReadbackResult read;
            auto* updates = rhi->nextResourceUpdateBatch();
            updates->readBackBuffer(geometry.instanceBuffer(), 0, 5 * 48, &read);
            cb->resourceUpdate(updates);
            QCOMPARE(rhi->endOffscreenFrame(), QRhi::FrameOpSuccess);
            QCOMPARE(read.data.size(), qsizetype(5 * 48));
            float output[60]; std::memcpy(output, read.data.constData(), sizeof(output));
            for (int i = 0; i < 5; ++i) {
                const float* target = i == 0 ? edge : i == 1 || i == 4 ? highlight : i == 2 ? dim : items[i].color;
                for (int c = 0; c < 4; ++c)
                    QVERIFY(std::abs(output[i * 12 + 8 + c] - (items[i].color[c] + (target[c] - items[i].color[c]) * amount)) < 1e-5f);
                QVERIFY(std::abs(output[i * 12 + 4] - (i == 4 && amount > 0 ? 4.4f : 4.0f)) < 1e-5f);
            }
            // Same revision: mutated CPU source must not affect resident data.
            // The reference colors above stay unchanged for the later frames.
            if (amount == 0.25f) {
                auto replacement = items;
                for (auto& item : replacement) item.color[0] = 0.9f;
                QCOMPARE(rhi->beginOffscreenFrame(&cb), QRhi::FrameOpSuccess);
                QVERIFY(geometry.record(rhi.get(), cb, gpu.stateBuffer(), replacement, viewport, 1, &style));
                QCOMPARE(rhi->endOffscreenFrame(), QRhi::FrameOpSuccess);
            }
        }
    }

    void movingTextKeepsItsRasterShape()
    {
        ForceViewRhiWidget view;
        view.resize(600, 400);
        view.setSimulationBackendMode("gpu");
        view.setManyBodyStrength(0); view.setLinkStrength(0); view.setCollisionStrength(0);
        view.setCenterStrength(0.01f);
        view.setTextColor(Qt::black); view.setHoverColor(Qt::red);
        view.setTextThresholdFactor(0.1f);
        view.setGraph(1, {}, {60, 0}, {"moving"}, {"MMMM"}, {4}, {Qt::red});
        view.show(); QTest::mouseMove(&view, QPoint(1, 1));
        QTest::qWait(200); view.fitViewToContent();
        auto* canvas = view.findChild<QRhiWidget*>(); QVERIFY(canvas);
        QRect previousBounds;
        QImage reference;
        double worst = 0;
        int minimumInk = 1000000000, maximumInk = 0;
        for (int frame = 0; frame < 30; ++frame) {
            QTest::qWait(16);
            const auto image = canvas->grabFramebuffer().convertToFormat(QImage::Format_RGBA8888);
            QRect bounds;
            int ink = 0;
            for (int y = 0; y < image.height(); ++y) for (int x = 0; x < image.width(); ++x) {
                const auto color = image.pixelColor(x, y);
                if (color.red() < 160 && color.green() < 160 && color.blue() < 160) {
                    bounds = bounds.united(QRect(x, y, 1, 1)); ++ink;
                }
            }
            QVERIFY2(ink > 100, "Text disappeared during motion");
            minimumInk = std::min(minimumInk, ink); maximumInk = std::max(maximumInk, ink);
            const auto glyphs = image.copy(bounds);
            if (reference.isNull()) reference = glyphs;
            QCOMPARE(glyphs.size(), reference.size());
            double difference = 0;
            for (int y = 0; y < glyphs.height(); ++y) for (int x = 0; x < glyphs.width() * 4; ++x)
                difference += std::abs(int(glyphs.constScanLine(y)[x]) - int(reference.constScanLine(y)[x]));
            difference /= glyphs.width() * glyphs.height() * 4;
            worst = std::max(worst, difference);
            if (!previousBounds.isEmpty()) QVERIFY2(bounds.x() <= previousBounds.x(), "Moving label jumped against node motion");
            previousBounds = bounds;
        }
        qInfo() << "moving text worst aligned image difference" << worst << "ink range" << minimumInk << maximumInk;
        QVERIFY2(worst < 2.0, qPrintable(QString("Moving text raster fluctuates: %1").arg(worst)));
    }

    void gpuVisibleTextSurvivesAnOlderCpuSnapshot()
    {
        ForceViewRhiWidget view;
        view.resize(240, 200); view.setSimulationBackendMode("gpu");
        view.setManyBodyStrength(0); view.setCenterStrength(0);
        view.setLinkStrength(0); view.setCollisionStrength(0);
        view.setTextColor(Qt::black); view.setTextThresholdFactor(0.1f);
        view.setGraph(1, {}, {60, 0}, {"visible"}, {"MMMM"}, {4}, {Qt::red});
        view.show(); QTest::mouseMove(&view, QPoint(1, 1));
        QTest::qWait(300); view.pauseSimulation(); QTest::qWait(100);
        auto* canvas = view.findChild<QRhiWidget*>(); QVERIFY(canvas);
        canvas->grabFramebuffer();
        QVERIFY(view.m_gpuSimulation && view.m_gpuSimulation->stateBuffer());
        // Eight in-flight steps can differ by 8 * 15 world units. The GPU
        // position remains on screen while the older CPU snapshot is outside.
        view.m_zoom = 1; view.m_panX = view.m_panY = 0;
        view.m_physicsState->updateRenderPosAt(0, 180, 0);
        const auto image = canvas->grabFramebuffer();
        QVERIFY2(!view.m_textVertices.empty(), "GPU-visible text was culled using an older CPU snapshot");
        int ink = 0;
        for (int y = 0; y < image.height(); ++y) for (int x = 0; x < image.width(); ++x) {
            const auto color = image.pixelColor(x, y);
            if (color.red() < 200 && color.green() < 200 && color.blue() < 200) ++ink;
        }
        QVERIFY(ink > 40);
    }

    void largeGraphLabelBudget()
    {
        ForceViewRhiWidget view;
        QVector<float> positions;
        QStringList ids;
        for (int i = 0; i < 2500; ++i) { positions << float(i % 50) * 10 << float(i / 50) * 10; ids << QString::number(i); }
        view.setManyBodyStrength(0); view.setCenterStrength(0); view.setLinkStrength(0); view.setCollisionStrength(0);
        view.resize(800, 600);
        view.setGraph(2500, {}, positions, ids, ids, QVector<float>(2500, 2));
        QSignalSpy details(&view, &ForceViewRhiWidget::performanceDetailsUpdated);
        view.show();
        QTRY_VERIFY_WITH_TIMEOUT(!details.isEmpty(), 5000);
        QTest::qWait(150); view.fitViewToContent(); view.update();
        QTRY_VERIFY_WITH_TIMEOUT(details.size() >= 2, 5000);
        const auto d = details.last()[0].toMap();
        QVERIFY(d.value("gpuInstances").toBool());
        QVERIFY(d.value("textVertices").toInt() > 0);
        QVERIFY(d.value("textVertices").toInt() <= 2001 * 4 * 6);
    }

    void labelSelectionSurvivesCameraMotion_data()
    {
        QTest::addColumn<bool>("gpu");
        QTest::newRow("cpu") << false;
        QTest::newRow("gpu") << true;
    }

    void labelSelectionSurvivesCameraMotion()
    {
        QFETCH(bool, gpu);
        ForceViewRhiWidget view;
        constexpr int count = 2500;
        QVector<float> positions;
        QStringList ids, labels;
        for (int i = 0; i < count; ++i) {
            positions << (i < 2 ? 79.0f + 2.0f * i : 20000.0f + i * 20.0f) << 0.0f;
            ids << QString::number(i); labels << QStringLiteral("T");
        }
        view.resize(800, 600);
        view.setSimulationBackendMode(gpu ? "gpu" : "cpu");
        view.setGraph(count, {}, positions, ids, labels, QVector<float>(count, 2));
        view.pauseSimulation();
        view.show();
        QTRY_VERIFY_WITH_TIMEOUT(view.m_labelLayoutByIndex.size() == count
            && view.m_labelLayoutByIndex[0] && !view.m_labelLayoutByIndex[0]->quads.empty()
            && (!gpu || view.m_usingGpuGeometry), 5000);
        view.m_renderPosSnapshot.assign(positions.begin(), positions.end());
        const float dpr = float(view.findChild<QRhiWidget*>()->devicePixelRatioF());
        view.m_renderPosSnapshot[0] = 79.0f / dpr;
        view.m_renderPosSnapshot[2] = 81.0f / dpr;
        view.m_retainedLabels.clear();
        view.m_zoom = 1;
        view.m_panX = view.m_panY = 0;
        view.prepareTextVertices();
        auto containsLabel = [&](int node) {
            return std::any_of(view.m_textVertices.begin(), view.m_textVertices.end(),
                [node](const auto& vertex) { return int(vertex.node) == node; });
        };
        QVERIFY(containsLabel(1));
        for (const float pan : {1.0f, 2.0f, 3.0f, 0.0f, -2.0f}) {
            view.m_panX = pan;
            view.prepareTextVertices();
            QVERIFY2(containsLabel(1), "Visible label vanished at a camera-dependent density-cell boundary");
        }
        view.m_panX = 0;
        for (const float zoom : {1.01f, 0.99f, 1.02f, 0.98f, 1.0f}) {
            view.m_zoom = zoom;
            view.prepareTextVertices();
            QVERIFY2(containsLabel(1), "Visible label vanished during a small zoom change");
        }
        view.m_panX = 20040;
        view.prepareTextVertices();
        QVERIFY(!containsLabel(1));
        QVERIFY2(containsLabel(2), "Old retained labels prevented newly visible labels from entering");
    }

    void optimizationBenchmark()
    {
        if (!qEnvironmentVariableIsSet("DARKEYE_GPU_LAYOUT_BENCHMARK")) QSKIP("Optional optimization benchmark");
        for (int count : {10000, 100000}) {
            PhysicsState state;
            fill(state, count);
            const float scale = (std::sqrt(float(count)) * 25.0f + 50.0f) / 150.0f;
            for (float& x : state.pos) x *= scale;
            state.syncDragPosFromPos();
            for (bool optimized : {false, true}) {
                std::array<double, 5> samples;
                GpuSimulation::Timings timings;
                for (double& sample : samples) {
                    GpuSimulation gpu;
                    QVERIFY(gpu.initialize(rhi.get(), state, nullptr, optimized));
                    std::vector<float> output;
                    QVERIFY(step(gpu, state, {}, output));
                    QElapsedTimer timer; timer.start();
                    for (int i = 0; i < 5; ++i) QVERIFY(step(gpu, state, {}, output));
                    sample = timer.nsecsElapsed() / 5e6;
                    gpu.setProfilingEnabled(true);
                    QVERIFY(step(gpu, state, {}, output));
                    timings = gpu.timings();
                }
                std::sort(samples.begin(), samples.end());
                qInfo() << "optimization" << count << (optimized ? "radix/fused" : "reference bitonic")
                        << "ms/step" << samples[2] << "dispatches" << timings.dispatches
                        << "GPU bounds/sort/tree/force" << timings.gpuBoundsMs << timings.gpuSortMs << timings.gpuTreeMs << timings.gpuForceMs;
            }
        }
    }

    void viewPreparationBenchmark()
    {
        if (!qEnvironmentVariableIsSet("DARKEYE_GPU_LAYOUT_BENCHMARK")) QSKIP("Optional view benchmark");
        constexpr int count = 100000;
        QVector<float> positions;
        QVector<int> edges;
        QStringList ids;
        std::mt19937 rng(42);
        const float extent = std::sqrt(float(count)) * 25.0f + 50.0f;
        std::uniform_real_distribution<float> random(-extent, extent);
        for (int i = 0; i < count; ++i) {
            positions << random(rng) << random(rng); ids << QString::number(i);
            if (i) edges << i - 1 << i;
        }
        for (const QString backend : {QStringLiteral("cpu"), QStringLiteral("gpu")}) {
            ForceViewRhiWidget view;
            view.resize(1000, 700);
            view.setSimulationBackendMode(backend);
            view.setManyBodyStrength(0); view.setCenterStrength(0);
            view.setLinkStrength(0); view.setCollisionStrength(0);
            view.setGraph(count, edges, positions, ids, ids, QVector<float>(count, 4));
            if (backend == QStringLiteral("cpu")) view.pauseSimulation();
            QSignalSpy details(&view, &ForceViewRhiWidget::performanceDetailsUpdated);
            auto* canvas = view.findChild<QRhiWidget*>(); QVERIFY(canvas);
            QTimer refresh; refresh.setInterval(16);
            connect(&refresh, &QTimer::timeout, canvas, [canvas]() { canvas->update(); });
            refresh.start(); view.show();
            QTRY_VERIFY_WITH_TIMEOUT(details.size() >= 4, 10000);
            QCOMPARE(details.last()[0].toMap().value("gpuInstances").toBool(), backend == QStringLiteral("gpu"));
            std::vector<double> geometry, text;
            for (int i = 1; i < details.size(); ++i) {
                const auto d = details[i][0].toMap();
                geometry.push_back(d.value("geometryCpuMs").toDouble());
                text.push_back(d.value("textCpuMs").toDouble());
            }
            std::sort(geometry.begin(), geometry.end()); std::sort(text.begin(), text.end());
            qInfo() << "view 100000" << backend << "median CPU geometry ms" << geometry[geometry.size()/2]
                    << "text ms" << text[text.size()/2]
                    << "GPU instances" << details.last()[0].toMap().value("gpuInstances");
        }
    }

    void hoverGeometryPreparationBenchmark()
    {
        if (!qEnvironmentVariableIsSet("DARKEYE_GPU_HOVER_BENCHMARK")) QSKIP("Optional hover preparation benchmark");
        constexpr int count = 100000;
        QVector<float> positions;
        QVector<int> edges;
        QStringList ids;
        for (int i = 0; i < count; ++i) {
            positions << float(i % 400) * 20 << float(i / 400) * 20;
            ids << QString::number(i);
            if (i) edges << i - 1 << i;
        }
        ForceViewRhiWidget view;
        view.resize(1000, 700);
        view.setSimulationBackendMode("gpu");
        view.setManyBodyStrength(0); view.setCenterStrength(0);
        view.setLinkStrength(0); view.setCollisionStrength(0);
        view.setTextThresholdFactor(100);
        view.setGraph(count, edges, positions, ids, ids, QVector<float>(count, 4));
        view.show();
        QTRY_VERIFY_WITH_TIMEOUT(view.m_gpuSimulation && view.m_usingGpuGeometry, 5000);
        view.pauseSimulation();
        QTRY_VERIFY_WITH_TIMEOUT(view.m_gpuSimulation && view.m_usingGpuGeometry, 5000);
        view.m_hoverIndex = count / 2;
        view.updateNeighborMaskForHover(view.m_hoverIndex);
        std::vector<double> samples;
        for (int frame = 0; frame < 33; ++frame) {
            view.m_hoverGlobal = float(frame + 1) / 34;
            QRhiCommandBuffer* cb = nullptr;
            QCOMPARE(view.m_rhi->beginOffscreenFrame(&cb), QRhi::FrameOpSuccess);
            QElapsedTimer timer; timer.start();
            QVERIFY(view.prepareGpuGeometry(cb));
            const double ms = timer.nsecsElapsed() / 1e6;
            QCOMPARE(view.m_rhi->endOffscreenFrame(), QRhi::FrameOpSuccess);
            if (frame) samples.push_back(ms);
        }
        std::sort(samples.begin(), samples.end());
        qInfo() << "100000 nodes + 99999 edges, hover animation CPU geometry median ms:" << samples[samples.size()/2];
    }

    void approximationError()
    {
        PhysicsState state;
        fill(state, 2048);
        GpuSimulation exact, approximate;
        QVERIFY(exact.initialize(rhi.get(), state));
        QVERIFY(approximate.initialize(rhi.get(), state));
        GpuSimulation::Parameters p;
        p.maxDisplacement = 1e8f;
        p.link = p.center = p.collisionStrength = 0.0f;
        p.theta = 0.0f;
        std::vector<float> reference, output;
        QVERIFY(step(exact, state, p, reference));
        p.theta = 0.6f;
        QVERIFY(step(approximate, state, p, output));
        double error2 = 0.0, norm2 = 0.0;
        for (int i = 0; i < state.nNodes; ++i)
            for (int axis = 2; axis < 4; ++axis) {
                const double diff = output[4 * i + axis] - reference[4 * i + axis];
                error2 += diff * diff;
                norm2 += double(reference[4 * i + axis]) * reference[4 * i + axis];
            }
        const double relative = std::sqrt(error2 / std::max(norm2, 1e-20));
        qInfo() << "Barnes-Hut relative RMS force error:" << relative;
        QVERIFY(relative < 0.12);
    }

    void batchedStepsMatchResidentSingleSteps()
    {
        PhysicsState state;
        fill(state, 129);
        GpuSimulation singles, batched;
        QVERIFY(singles.initialize(rhi.get(), state));
        QVERIFY(batched.initialize(rhi.get(), state));
        GpuSimulation::Parameters p;
        std::vector<GpuSimulation::Parameters> parameters;
        std::vector<float> expected, result;
        for (int i = 0; i < 4; ++i) {
            p.alpha = 1.0f - i * 0.01f;
            parameters.push_back(p);
            QVERIFY(step(singles, state, p, expected));
        }
        QRhiCommandBuffer* cb = nullptr;
        QCOMPARE(rhi->beginOffscreenFrame(&cb), QRhi::FrameOpSuccess);
        QVERIFY(batched.recordSteps(cb, state, parameters));
        QCOMPARE(rhi->endOffscreenFrame(), QRhi::FrameOpSuccess);
        QVERIFY(batched.takeResult(result));
        QCOMPARE(result.size(), expected.size());
        for (size_t i = 0; i < result.size(); ++i)
            QVERIFY(std::abs(result[i] - expected[i]) < 1e-5f);
    }

    void gpuCoolingMatchesCpuAcrossBatchBoundaries()
    {
        PhysicsState state, cpu;
        fill(state, 1);
        fill(cpu, 1);
        Simulation preview(&state), reference(&cpu);
        preview.start();
        reference.start();
        for (int batch = 0; batch < 50; ++batch) {
            QVERIFY(preview.prepareGpuTick());
            const auto alphas = preview.gpuTickAlphas(4);
            QCOMPARE(alphas.size(), size_t(4));
            for (float alpha : alphas) {
                QCOMPARE(alpha, reference.alpha());
                reference.tick();
                preview.completeGpuTick(1.0f);
            }
            QCOMPARE(preview.tickCount(), reference.tickCount());
            QCOMPARE(preview.alpha(), reference.alpha());
        }
    }

    void convergenceUsesSpeedBeforeClamping()
    {
        PhysicsState state;
        state.init(2, {});
        state.pos = {100, 0, -100, 0};
        state.syncDragPosFromPos();
        state.dragging[0] = 1;
        GpuSimulation gpu;
        QVERIFY(gpu.initialize(rhi.get(), state));
        GpuSimulation::Parameters p;
        p.manyBody = p.link = p.collisionStrength = 0.0f;
        p.center = 10000.0f;
        QRhiCommandBuffer* cb = nullptr;
        QCOMPARE(rhi->beginOffscreenFrame(&cb), QRhi::FrameOpSuccess);
        QVERIFY(gpu.recordStep(cb, state, p));
        QCOMPARE(rhi->endOffscreenFrame(), QRhi::FrameOpSuccess);
        std::vector<float> result;
        float speed = 0.0f;
        QVERIFY(gpu.takeResult(result, nullptr, &speed));
        QVERIFY(std::abs(speed - 1500000.0f) < 1.0f);
        QCOMPARE(result[0], 100.0f);
        QCOMPARE(result[2], -750000.0f); // CPU does not clamp pinned velocities
        QVERIFY(std::abs(result[6] - 150.0f) < 0.001f);
    }

    void rejectUnsupportedAndEmpty()
    {
        PhysicsState empty;
        GpuSimulation gpu;
        QVERIFY(!gpu.initialize(rhi.get(), empty));
        QVERIFY(!gpu.initialize(nullptr, empty));
    }

    void coolsAndSettlesWithoutInvalidPositions()
    {
        PhysicsState state;
        fill(state, 64);
        GpuSimulation gpu;
        QVERIFY(gpu.initialize(rhi.get(), state));
        Simulation clock(&state);
        clock.start();
        std::vector<float> previous = state.pos;
        while (clock.prepareGpuTick() && clock.tickCount() < 1000) {
            GpuSimulation::Parameters p;
            p.alpha = clock.alpha();
            QRhiCommandBuffer* cb = nullptr;
            QCOMPARE(rhi->beginOffscreenFrame(&cb), QRhi::FrameOpSuccess);
            QVERIFY(gpu.recordStep(cb, state, p));
            QCOMPARE(rhi->endOffscreenFrame(), QRhi::FrameOpSuccess);
            std::vector<float> result;
            float speed = 0.0f;
            QVERIFY(gpu.takeResult(result, nullptr, &speed));
            for (int i = 0; i < state.nNodes; ++i) {
                const float dx = result[4 * i] - previous[2 * i];
                const float dy = result[4 * i + 1] - previous[2 * i + 1];
                QVERIFY(std::hypot(dx, dy) <= 15.001f);
                QVERIFY(std::abs(result[4 * i]) < 10000.0f);
                QVERIFY(std::abs(result[4 * i + 1]) < 10000.0f);
                previous[2 * i] = result[4 * i];
                previous[2 * i + 1] = result[4 * i + 1];
            }
            clock.completeGpuTick(speed);
        }
        QVERIFY(clock.tickCount() >= 150);
        QVERIFY(clock.tickCount() < 1000);
        QVERIFY(!clock.isActive());
    }

    void widgetLifecycleAndTopology()
    {
        ForceViewRhiWidget view;
        view.setSimulationBackendMode(QStringLiteral("gpu"));
        view.resize(600, 400);
        view.setGraph(3, {0, 1, 1, 2}, {-30, 0, 0, 30, 30, 0},
                      {"a", "b", "c"}, {"a", "b", "c"}, {4, 4, 4});
        QSignalSpy ticks(&view, &ForceViewRhiWidget::tickTimeUpdated);
        view.show();
        QTRY_COMPARE_WITH_TIMEOUT(view.activeSimulationBackend(), QStringLiteral("gpu"), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(ticks.size() >= 3, 5000);
        view.pauseSimulation();
        const QPointF paused = view.getNodePosition("a");
        const int pausedTicks = ticks.size();
        QTest::qWait(100);
        QCOMPARE(view.getNodePosition("a"), paused);
        QCOMPARE(ticks.size(), pausedTicks);
        view.setDragging(0, true);
        QTRY_VERIFY_WITH_TIMEOUT(ticks.size() > pausedTicks + 1, 5000);
        QCOMPARE(view.getNodePosition("a"), paused);
        view.setDragging(0, false);
        view.add_node_runtime("d", 10, 10, "d", 4);
        view.add_edge_runtime("c", "d");
        view.remove_node_runtime("b");
        QCOMPARE(view.getNodeIds(), QStringList({"a", "c", "d"}));
        const int changedTicks = ticks.size();
        QTRY_VERIFY_WITH_TIMEOUT(ticks.size() > changedTicks + 1, 5000);
        view.pauseSimulation();
        view.setSimulationBackendMode("cpu");
        QCOMPARE(view.activeSimulationBackend(), QStringLiteral("cpu"));
        QTest::qWait(60);
        const int cpuTicks = ticks.size();
        view.resumeSimulation();
        QTRY_VERIFY_WITH_TIMEOUT(ticks.size() > cpuTicks, 5000);
        view.setSimulationBackendMode("gpu");
        QTRY_COMPARE_WITH_TIMEOUT(view.activeSimulationBackend(), QStringLiteral("gpu"), 5000);
        const int switchedTicks = ticks.size();
        QTRY_VERIFY_WITH_TIMEOUT(ticks.size() > switchedTicks + 1, 5000);
        view.setGraph(0, {}, {}, {}, {}, {});
        QVERIFY(view.getNodeIds().isEmpty());
        QTest::qWait(60);
    }

    void benchmark()
    {
        if (!qEnvironmentVariableIsSet("DARKEYE_GPU_LAYOUT_BENCHMARK"))
            QSKIP("Set DARKEYE_GPU_LAYOUT_BENCHMARK=1 for CPU/GPU end-to-end timing");
        for (const bool dense : {true, false}) for (const int count : {400, 2000, 10000}) {
            std::array<double, 5> cpuSamples, gpuSamples;
            for (int sample = 0; sample < 5; ++sample) {
                PhysicsState state, cpu;
                fill(state, count);
                fill(cpu, count);
                if (!dense) {
                    const float scale = (std::sqrt(float(count)) * 25.0f + 50.0f) / 150.0f;
                    for (float& v : state.pos) v *= scale;
                    for (float& v : cpu.pos) v *= scale;
                    state.syncDragPosFromPos();
                    cpu.syncDragPosFromPos();
                }
                GpuSimulation gpu;
                QVERIFY(gpu.initialize(rhi.get(), state));
                Simulation sim(&cpu);
                cpuEngine(sim, 0.6f);
                GpuSimulation::Parameters p;
                std::vector<float> output;
                QVERIFY(step(gpu, state, p, output)); // shader/device warmup
                sim.tick();
                QElapsedTimer timer;
                timer.start();
                for (int n = 0; n < 10; ++n) sim.tick();
                cpuSamples[sample] = timer.nsecsElapsed() / 1e7;
                timer.restart();
                for (int n = 0; n < 10; ++n) QVERIFY(step(gpu, state, p, output));
                gpuSamples[sample] = timer.nsecsElapsed() / 1e7;
            }
            std::sort(cpuSamples.begin(), cpuSamples.end());
            std::sort(gpuSamples.begin(), gpuSamples.end());
            qInfo() << (dense ? "dense" : "application-scale") << count
                    << "nodes median of 5x10 steps, CPU ms/tick:" << cpuSamples[2]
                    << "GPU including submit/readback ms/tick:" << gpuSamples[2];
        }
    }
};

QTEST_MAIN(GpuSimulationTest)
#include "GpuSimulationTest.moc"
