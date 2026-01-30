
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <algorithm>
#include <iomanip>
#include "quantum_kernels.h"

// ==========================================
// Estructuras y Lectura
// ==========================================
enum State : uint8_t { H = 0, V = 1, D = 2, A = 3 };

struct detections_t {
    std::vector<State> measurements;
    std::vector<uint64_t> time_tags;
};

void read_detections(const std::string& file_name, detections_t& alice, detections_t& bob) {
    alice.measurements.clear(); alice.time_tags.clear();
    bob.measurements.clear(); bob.time_tags.clear();

    auto load_file = [](const std::string& path, detections_t& det) {
        std::ifstream file(path);
        if (!file.is_open()) return;
        char next_char = 0;
        std::string int_str;
        while(file >> next_char) {
            if (next_char == '(') {
                std::getline(file, int_str, ',');
                det.time_tags.push_back(strtoull(int_str.c_str(), NULL, 10));
                std::getline(file, int_str, ')');
                det.measurements.push_back(static_cast<State>(atoi(int_str.c_str())));
            }
        }
    };
    load_file(file_name + "_alice.detections", alice);
    load_file(file_name + "_bob.detections", bob);
}

// ==========================================
// GOLDEN MODEL (Referencia Software)
// ==========================================
// Esta función hace lo mismo que el HW pero en C++ estándar para validar
std::vector<uint8_t> software_sifting(
    const detections_t& alice,
    const detections_t& bob,
    int16_t lag,
    uint64_t window
) {
    std::vector<uint8_t> key;
    size_t a_ptr = 0;

    // Recorremos todos los eventos de Bob
    for (size_t b_idx = 0; b_idx < bob.time_tags.size(); ++b_idx) {
        if (b_idx >= BATCH_LIMIT) break;

        uint64_t target = bob.time_tags[b_idx] + lag;
        uint64_t min_range = target - window;
        uint64_t max_range = target + window;

        // Base de Bob: 0/1=Rect(0), 2/3=Diag(1)
        bool b_diag = (bob.measurements[b_idx] >= 2);

        // Búsqueda en Alice (sin optimizaciones complejas, fuerza bruta simple)
        for (size_t i = 0; i < ALICE_LIMIT; ++i) { // Miramos 1024 adelante para estar seguros
            size_t idx = a_ptr + i;
            if (idx >= alice.time_tags.size() || idx >= BATCH_LIMIT) break;

            uint64_t a_tag = alice.time_tags[idx];

            // Si nos pasamos de tiempo, cortamos para eficiencia básica
            if (a_tag > max_range + ALICE_LIMIT) break;

            if (a_tag >= min_range && a_tag <= max_range) {
                // Coincidencia Temporal encontrada
                bool a_diag = (alice.measurements[idx] >= 2);

                // Coincidencia de Base (BB84)
                if (a_diag == b_diag) {
                    // Bit: H/D(pares)->0, V/A(impares)->1
                    key.push_back(alice.measurements[idx] % 2);
                }

                // Avanzamos puntero (asumimos 1 coincidencia por pulso)
                a_ptr = idx + 1;
                break;
            }
        }
    }
    return key;
}

// ==========================================
// MAIN
// ==========================================
int main() {
    std::cout << "============================================================\n";
    std::cout << "    AURORA HLS: HW vs SW GOLDEN MODEL VALIDATION\n";
    std::cout << "============================================================\n";

    // Buffers simulados
    std::vector<uint8_t> alice_meas_buf(BATCH_LIMIT);
    std::vector<uint8_t> bob_meas_buf(BATCH_LIMIT);
    std::vector<uint8_t> hls_key_buffer(BATCH_LIMIT);

    for (int i = 1; i <= 20; ++i) {
        std::string batch_path = "../../../../batches/batch" + std::to_string(i);
        detections_t alice_det, bob_det;
        read_detections(batch_path, alice_det, bob_det);

        if (alice_det.time_tags.empty()) continue;

        // Preparar datos para HW
        uint32_t size_a = std::min((size_t)BATCH_LIMIT, alice_det.time_tags.size());
        uint32_t size_b = std::min((size_t)BATCH_LIMIT, bob_det.time_tags.size());
        for(uint32_t k=0; k<size_a; k++) alice_meas_buf[k] = (uint8_t)alice_det.measurements[k];
        for(uint32_t k=0; k<size_b; k++) bob_meas_buf[k] = (uint8_t)bob_det.measurements[k];

        // 1. EJECUCIÓN HARDWARE (HLS)
        uint32_t hls_count = 0;
        CorrResult hls_res;

        // Paso A: Sync
        compute_correlation_hls(
            alice_det.time_tags.data(), alice_meas_buf.data(),
            bob_det.time_tags.data(), bob_meas_buf.data(),
            size_a, size_b, 350, 0, MODE_SYNC,
            hls_key_buffer.data(), &hls_count, hls_res
        );
        int16_t lag = hls_res.best_lag;

        // Paso B: Sift (Ventana 350ps)
        compute_correlation_hls(
            alice_det.time_tags.data(), alice_meas_buf.data(),
            bob_det.time_tags.data(), bob_meas_buf.data(),
            size_a, size_b, 350, lag, MODE_SIFT,
            hls_key_buffer.data(), &hls_count, hls_res
        );

        // 2. EJECUCIÓN SOFTWARE (GOLDEN MODEL)
        // Usamos exactamente los mismos parámetros: Lag y Ventana
        std::vector<uint8_t> sw_key = software_sifting(alice_det, bob_det, lag, 350);

        // 3. COMPARACIÓN
        std::cout << "Batch " << std::setw(2) << i << " | Lag: " << std::setw(4) << lag;

        bool perfect_match = true;
        if (hls_count != sw_key.size()) {
            perfect_match = false;
        } else {
            for(size_t k=0; k<hls_count; k++) {
                if (hls_key_buffer[k] != sw_key[k]) { perfect_match = false; break; }
            }
        }

        if (perfect_match) {
            std::cout << " | Sifted Bits: " << std::setw(3) << hls_count
                      << " | HW vs SW: [MATCH OK]" << std::endl;
        } else {
            std::cout << " | HW: " << hls_count << " SW: " << sw_key.size()
                      << " | HW vs SW: [MISMATCH FAIL]" << std::endl;

            // Debug de diferencias
            std::cout << "    -> HW Key: ";
            for(size_t k=0; k<std::min((size_t)hls_count, (size_t)16); k++) std::cout << (int)hls_key_buffer[k];
            std::cout << "\n    -> SW Key: ";
            for(size_t k=0; k<std::min(sw_key.size(), (size_t)16); k++) std::cout << (int)sw_key[k];
            std::cout << "\n";
        }
    }
    std::cout << "============================================================\n";
    return 0;
}

//
///// _______________
//
//#include <iostream>
//#include <fstream>
//#include <vector>
//#include <string>
//#include <algorithm>
//#include <iomanip>
//#include "quantum_kernels.h"
//
//// ==========================================
//// Estructuras y Lectura
//// ==========================================
//enum State : uint8_t { H = 0, V = 1, D = 2, A = 3 };
//
//struct detections_t {
//    std::vector<State> measurements;
//    std::vector<uint64_t> time_tags;
//};
//
//void read_detections(const std::string& file_name, detections_t& alice, detections_t& bob) {
//    alice.measurements.clear(); alice.time_tags.clear();
//    bob.measurements.clear(); bob.time_tags.clear();
//
//    // Lectura robusta saltando cabeceras si es necesario
//    auto load_file = [](const std::string& path, detections_t& det) {
//        std::ifstream file(path);
//        if (!file.is_open()) return;
//        char next_char = 0;
//        std::string int_str;
//        while(file >> next_char) {
//            if (next_char == '(') {
//                std::getline(file, int_str, ',');
//                det.time_tags.push_back(strtoull(int_str.c_str(), NULL, 10));
//                std::getline(file, int_str, ')');
//                det.measurements.push_back(static_cast<State>(atoi(int_str.c_str())));
//            }
//        }
//    };
//
//    load_file(file_name + "_alice.detections", alice);
//    load_file(file_name + "_bob.detections", bob);
//}
//
//// ==========================================
//// MAIN: Validación Alice vs Bob
//// ==========================================
//int main() {
//    std::cout << "============================================================\n";
//    std::cout << "       AURORA HLS: ALICE vs BOB CONSISTENCY CHECK\n";
//    std::cout << "============================================================\n";
//
//    // Buffers simulados (RAM)
//    std::vector<uint8_t> alice_meas_buf(BATCH_LIMIT);
//    std::vector<uint8_t> bob_meas_buf(BATCH_LIMIT);
//
//    // Buffers de salida para las claves
//    std::vector<uint8_t> alice_key_buf(BATCH_LIMIT);
//    std::vector<uint8_t> bob_key_buf(BATCH_LIMIT);
//
//    for (int i = 1; i <= 20; ++i) {
//        std::string batch_path = "../../../../batches/batch" + std::to_string(i);
//        detections_t alice_det, bob_det;
//        read_detections(batch_path, alice_det, bob_det);
//
//        if (alice_det.time_tags.empty()) continue;
//
//        uint32_t size_a = std::min((size_t)BATCH_LIMIT, alice_det.time_tags.size());
//        uint32_t size_b = std::min((size_t)BATCH_LIMIT, bob_det.time_tags.size());
//
//        // Cargar buffers de entrada
//        for(uint32_t k=0; k<size_a; k++) alice_meas_buf[k] = (uint8_t)alice_det.measurements[k];
//        for(uint32_t k=0; k<size_b; k++) bob_meas_buf[k] = (uint8_t)bob_det.measurements[k];
//
//        // Variables HLS
//        uint32_t alice_bits_found = 0;
//        uint32_t bob_bits_found = 0;
//        CorrResult res;
//
//        // ---------------------------------------------------------
//        // 1. SINCRONIZACIÓN (Encontrar Lag)
//        // ---------------------------------------------------------
//        compute_correlation_hls(
//            alice_det.time_tags.data(), alice_meas_buf.data(),
//            bob_det.time_tags.data(), bob_meas_buf.data(),
//            size_a, size_b,
//            350, // Ventana amplia para Sync
//            0,
//            MODE_SYNC,
//            alice_key_buf.data(), &alice_bits_found, // Dummy output
//            res
//        );
//        int16_t lag = res.best_lag;
//
//        // ---------------------------------------------------------
//        // 2. GENERAR CLAVE DE ALICE (Desde el punto de vista de Alice)
//        // ---------------------------------------------------------
//        // Nota: Para verificar, asumimos que Alice es la referencia (Lag 0)
//        // y Bob se ajusta, o viceversa. Aquí usaremos el Lag detectado para filtrar.
//
//        // Ejecución para ALICE (usando la ventana correcta y Lag detectado)
//        // Usamos ventana 350ps para asegurar que recuperamos los datos
//        compute_correlation_hls(
//            alice_det.time_tags.data(), alice_meas_buf.data(),
//            bob_det.time_tags.data(), bob_meas_buf.data(),
//            size_a, size_b,
//            350,
//            lag,
//            MODE_SIFT,
//            alice_key_buf.data(), &alice_bits_found,
//            res
//        );
//
//        // ---------------------------------------------------------
//        // 3. ANÁLISIS DE RESULTADOS
//        // ---------------------------------------------------------
//        std::cout << "Batch " << std::setw(2) << i
//                  << " | Lag: " << std::setw(4) << lag
//                  << " | Bits Found: " << std::setw(3) << alice_bits_found;
//
//        if (alice_bits_found > 0) {
//            std::cout << " | Sample Key: ";
//            for(size_t k=0; k<std::min((size_t)alice_bits_found, (size_t)16); k++)
//                std::cout << (int)alice_key_buf[k];
//            if(alice_bits_found > 16) std::cout << "...";
//            std::cout << " [VALID]";
//        } else {
//            std::cout << " [NO BITS]";
//        }
//        std::cout << std::endl;
//    }
//    return 0;
//}
