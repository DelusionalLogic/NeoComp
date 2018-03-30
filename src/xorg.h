#pragma once

#define GL_GLEXT_PROTOTYPES
#include <GL/glx.h>

#include <stdbool.h>
#include <stdio.h>

/// Print out an error message.
#define printf_err(format, ...) \
  fprintf(stderr, format "\n", ## __VA_ARGS__)

/// Print out an error message with function name.
#define printf_errf(format, ...) \
  printf_err("%s(): " format,  __func__, ## __VA_ARGS__)

struct X11Context {
    Display* display;
    int screen;
    GLXFBConfig* configs;
    int numConfigs;

    bool selected;
    GLXFBConfig selected_config;

    bool y_inverted;
};

bool xorgContext_init(struct X11Context* context, Display* display, int screen);

bool xorgContext_selectConfig(struct X11Context* context, VisualID visualid);

void xorgContext_delete(struct X11Context* context);
