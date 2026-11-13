// =============================================================================
// AURORA - Compatibilidad: hls::stream fuera de Vitis HLS
// =============================================================================
// Con Vitis HLS (csim y sintesis) se usa su <hls_stream.h>. Al compilar en un
// PC sin Vitis (tests con g++) se usa una implementacion minima equivalente
// para la C-simulation: FIFO sin limite de capacidad.
// =============================================================================
#ifndef AURORA_HLS_COMPAT_H
#define AURORA_HLS_COMPAT_H

#if defined(__has_include)
#if __has_include(<hls_stream.h>)
#include <hls_stream.h>
#define AURORA_HAVE_HLS_STREAM 1
#endif
#endif

#ifndef AURORA_HAVE_HLS_STREAM
#include <cstdio>
#include <cstdlib>
#include <deque>
namespace hls {
template <typename T>
class stream {
public:
    stream() {}
    explicit stream(const char *) {}
    void write(const T &v) { q_.push_back(v); }
    T read() {
        if (q_.empty()) {
            std::fprintf(stderr, "hls::stream (PC): lectura de un stream vacio\n");
            std::abort();
        }
        T v = q_.front();
        q_.pop_front();
        return v;
    }
    bool empty() const { return q_.empty(); }
    bool full() const { return false; }
    size_t size() const { return q_.size(); }
private:
    std::deque<T> q_;
};
} // namespace hls
#endif

#endif
