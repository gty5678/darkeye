#pragma once
#include <QApplication>
#include <optional>

// Returns no value when no test mode was requested.
std::optional<int> runAppTests(QApplication &app);
