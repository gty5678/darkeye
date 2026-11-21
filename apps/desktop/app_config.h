#pragma once

namespace darkeye {

inline constexpr int kRequiredPublicDbVersion = 2;
inline constexpr char kRequiredPrivateDbVersion[] = "1.1";

inline constexpr char kDefaultLatestJsonUrl[] =
    "https://darkeye.win/updates/stable/{target}/latest.json";

} // namespace darkeye
