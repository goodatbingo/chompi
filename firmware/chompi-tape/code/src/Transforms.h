/** Transforms
 *  Extension points for sound-transforming processes, inspired by the
 *  offline, "darkroom" style of tools like Argeiphontes Lyre.
 *
 *  Two kinds of process plug in here:
 *
 *  - FxTransform: a real-time stage inside the FX chain. It runs in the
 *    audio callback, once per block, so it must be cheap and must never block
 *    or touch the SD card.
 *
 *  - BackgroundJob: a slow, chunked process that runs from the main loop
 *    (outside the audio callback), for work too heavy for real time, such as
 *    rewriting a whole SDRAM buffer. Each Step() call should do a bounded
 *    amount of work and return quickly so the UI and SD streaming stay
 *    responsive. A job that writes to a buffer the audio callback also reads
 *    is responsible for its own synchronisation (e.g. write to a separate
 *    buffer and swap when done).
 *
 *  Neither interface owns memory or allocates: implementations are expected to
 *  be statically allocated, like everything else in this firmware.
 */
#pragma once
#include <cstddef>

namespace daisy
{
    /** Real-time processing stage, called once per audio block. */
    class FxTransform
    {
    public:
        /** Process a block in place.
         *  @param l, r  left and right working buffers, size samples each
         */
        virtual void Process(float* l, float* r, size_t size) = 0;

    protected:
        // Non-virtual and protected: transforms are never deleted through
        // this interface, and a virtual destructor would pull in operator
        // delete, which this firmware has no use for.
        ~FxTransform() {}
    };

    /** Chunked offline process, stepped from the main loop. */
    class BackgroundJob
    {
    public:
        /** Do one bounded chunk of work.
         *  @return true when the job has finished
         */
        virtual bool Step() = 0;

    protected:
        ~BackgroundJob() {}
    };
} // namespace daisy
