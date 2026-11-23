#pragma once
#include <QtCore>

#ifdef Q_OS_MACOS
inline constexpr const char *editorFont = "Menlo";
inline constexpr const char *previewFont = "PingFang SC";
inline constexpr const char *shortcutLabel = "⌘";
#elif defined(Q_OS_WIN)
inline constexpr const char *editorFont = "Cascadia Code";
inline constexpr const char *previewFont = "Microsoft YaHei UI";
inline constexpr const char *shortcutLabel = "Ctrl+";
#else
inline constexpr const char *editorFont = "monospace";
inline constexpr const char *previewFont = "sans-serif";
inline constexpr const char *shortcutLabel = "Ctrl+";
#endif

inline QString formulaResourcePath(){
#ifdef Q_OS_MACOS
    return QCoreApplication::applicationDirPath()+"/../Resources/res";
#else
    return QCoreApplication::applicationDirPath()+"/res";
#endif
}

