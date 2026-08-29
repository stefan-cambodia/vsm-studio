#include "TestFramework.h"
#include "vsm/audio/plugin/BuiltInPlugins.h"

int main() {
    vsm::audio::plugin::registerBuiltInPlugins();
    return vsm::test::runAll();
}
