/*
 * cc-edit — Chlorlite model editor
 * 
 * The primary model editor is the Python tool:
 *   python3 SKILL_DIR/tools/cc-model/cc-model.py
 *
 * This binary provides the same functionality for environments
 * where Python is unavailable. Coming in Chlorlite 0.3.
 */
#include <stdio.h>
int main(int argc, char** argv) {
    (void)argc; (void)argv;
    printf("Chlorlite Model Editor\n");
    printf("Primary editor: python3 SKILL_DIR/tools/cc-model/cc-model.py\n");
    printf("Run with --help for usage.\n");
    return 0;
}
