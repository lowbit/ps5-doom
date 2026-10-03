#ifndef TEST_PLAN_H
#define TEST_PLAN_H

#include "platform.h"

void test_plan_input(const char *steps);
void test_plan_captures(const char *frames);
int test_plan_has_input(void);
void test_plan_apply(int frame, pad_state_t *state);
int test_plan_captures_frame(int frame);
void test_plan_limit(int frames);
int test_plan_finished(int frame);

#endif
