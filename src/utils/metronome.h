#pragma once

#include <cmath>
#include <cstddef>

#include "utils/clock.h"

class Metronome
{
    // Desired tick length, measured in clock ticks.
    std::uint64_t tick_len = 0;
    // Max ticks per frame. 0 = no limit.
    int max_ticks = 0;

    // Accumulates clock ticks, which we then spend on our own ticks.
    std::uint64_t accumulator = 0;
    // If this is true, we're between frames now.
    bool new_frame = false;
    // This is set to true whenever the `max_ticks` limit is reached.
    bool lag = false;

    // Compensation logic. This makes the clock run more smooth.
    // When `accumulator` is very close to `tick_len` (absolute difference less than `comp_th * tick_len`),
    // it's adjusted by offsetting it by `+-comp_amount * tick_len`.
    // The adjustement direction is determined by `comp_dir`.
    float comp_th = 0;
    float comp_amount = 0;
    int comp_dir = 0; // 1 = forward, -1 = backward, 0 = away from `tick_len`.

  public:
    // Tick counter.
    std::uint64_t ticks = 0;

    Metronome() {} // For any other constructor to work, the clock has to be initialized first.

    Metronome(double freq, int max_ticks_per_frame = 8, float compensation_threshold = 0.01f, float compensation_amount = 0.5f)
    {
        SetFrequency(freq);
        SetMaxTicksPerFrame(max_ticks_per_frame);
        SetCompensation(compensation_threshold, compensation_amount);
        Reset();
    }

    void SetFrequency(double freq)
    {
        tick_len = Clock::TicksPerSecond() / freq;
    }
    void SetMaxTicksPerFrame(int n) // Set to 0 to disable the limit.
    {
        max_ticks = n;
    }

    // Threshold should be positive and small, at least less than 1.
    // Amount should be at least two times larger (by a some margin) than threshold, otherwise it will break. 0.5 should give best results, but don't make it much larger.
    // When the abs('frame len' - 'tick len') / 'tick_len' < 'threshold', the compensator kicks in and adds or subtracts 'amount' * 'tick len' from the time.
    void SetCompensation(float threshold, float amount)
    {
        comp_th = threshold;
        comp_amount = amount;
    }
    void Reset()
    {
        accumulator = 0;
        new_frame = true;
        lag = false;
        comp_dir = 0;
        ticks = 0;
    }

    [[nodiscard]] bool Lag() // Flag resets after this function is called. The flag is set to 1 if the amount of ticks per last frame is at maximum value.
    {
        if (lag)
        {
            lag = false;
            return true;
        }
        return false;
    }

    [[nodiscard]] double Frequency() const
    {
        return Clock::TicksPerSecond() / double(tick_len);
    }

    [[nodiscard]] std::uint64_t ClockTicksPerTick() const
    {
        return tick_len;
    }

    [[nodiscard]] int MaxTicksPerFrame() const
    {
        return max_ticks;
    }

    // Usage: `while (Tick(...)) {...}` during each frame.
    // The `delta` is the frame delta. It's only used on the first iteration.
    bool Tick(std::uint64_t delta)
    {
        if (new_frame)
            accumulator += delta;

        // Compensate.
        if (std::abs(int64_t(accumulator - tick_len)) < tick_len * comp_th)
        {
            int dir;
            if (comp_dir)
                dir = -comp_dir;
            else
                dir = (accumulator < tick_len ? -1 : 1);

            comp_dir += dir;
            accumulator += tick_len * comp_amount * dir;
        }

        // Decide whether to tick.
        if (accumulator >= tick_len)
        {
            // Do tick.
            if (max_ticks && accumulator > tick_len * max_ticks)
            {
                accumulator = tick_len * max_ticks;
                lag = true;
            }
            accumulator -= tick_len;
            new_frame = false;
            ticks++;
            return true;
        }
        else
        {
            // Don't need more ticks.
            new_frame = true;
            return false;
        }
    }

    // The fractional time point inside of the current tick. Updated by `Tick()`.
    [[nodiscard]] double Time() const
    {
        return accumulator / double(tick_len);
    }
};
