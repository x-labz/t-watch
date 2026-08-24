#pragma once

#include "lgfx_twatch_v2.hpp"

// Starts the UI/compositor task (pinned to core 1, per CLAUDE.md section 8).
// It owns the panel, drives the watchface/battery views, polls touch for
// horizontal swipe navigation, and is the only task that ever draws.
void ui_task_start(LGFX_TWatch2020V2 &lcd);
