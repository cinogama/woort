#include "woort.h"

#include "woort_vm.h"

int main(int argc, char** argv) {
    woort_init();

    woort_shutdown();
    return 0;
}