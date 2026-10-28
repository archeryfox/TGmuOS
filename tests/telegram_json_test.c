#include "keyboard.h"
#include "telegram.h"

#include <string.h>

int main(int argc, char** argv) {
    if (argc == 2 && strcmp(argv[1], "--keyboard-test") == 0) {
        return keyboard_self_test() ? 0 : 1;
    }
    return tg_json_self_test() ? 0 : 1;
}
