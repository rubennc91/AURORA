#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <algorithm>
#include <iomanip>
#include "quantum_kernels.h"

// ==========================================
// ESTRUCTURAS DE DATOS
// ==========================================
enum State : uint8_t { H = 0, V = 1, D = 2, A = 3 };

struct detections_t {
    std::vector<State> measurements;
    std::vector<uint64_t> time_tags;
};

// ==========================================
// FUNCIÓN DE LECTURA DE FICHEROS
// ==========================================
void read_detections(const std::string& file_name, detections_t& alice, detections_t& bob) {
    alice.measurements.clear(); alice.time_tags.clear();
    bob.measurements.clear(); bob.time_tags.clear();

    // Lambda para leer un fichero individual
    auto load_file = [](const std::string& path, detections_t& det) {
        std::ifstream file(path);
        if (!file.is_open()) return;

        char next_char = 0;
        std::string int_str;

        // Formato esperado: (Time, Meas), (Time, Meas)...
        while(file >> next_char) {
            if (next_char == '(') {
                // Leer Tiempo
                std::getline(file, int_str, ',');
                det.time_tags.push_back(strtoull(int_str.c_str(), NULL, 10));

                // Leer Medición
                std::getline(file, int_str, ')');
                det.measurements.push_back(static_cast<State>(atoi(int_str.c_str())));
            }
        }
    };

    load_file(file_name + "_alice.detections", alice);
    load_file(file_name + "_bob.detections", bob);
}

// ==========================================
// MAIN: SISTEMA COMPLETO CON ROBUSTEZ (PADDING)
// ==========================================
int main() {
    std::cout << "============================================================\n";
    std::cout << "    AURORA HLS: FULL SYSTEM (ROBUST PAD CHECK)\n";
    std::cout << "============================================================\n";

    // Buffers de Memoria (Simulando DDR)
    std::vector<uint8_t> a_meas(BATCH_LIMIT), b_meas(BATCH_LIMIT);
    std::vector<uint8_t> a_key(BATCH_LIMIT), b_key(BATCH_LIMIT);
    std::vector<uint8_t> a_shuf(BATCH_LIMIT), b_shuf(BATCH_LIMIT);
    std::vector<uint8_t> a_par(BATCH_LIMIT), b_par(BATCH_LIMIT);

    // Iteramos por los 100 Batches
    for (int i = 1; i <= 100; ++i) {
        std::string batch_path = "../../../../batches/batch" + std::to_string(i);
        detections_t alice_det, bob_det;

        read_detections(batch_path, alice_det, bob_det);

        if (alice_det.time_tags.empty()) {
            // std::cout << "Batch " << i << " empty/not found.\n";
            continue;
        }

        // Preparar buffers de entrada
        uint32_t sz_a = std::min((size_t)BATCH_LIMIT, alice_det.time_tags.size());
        uint32_t sz_b = std::min((size_t)BATCH_LIMIT, bob_det.time_tags.size());

        for(uint32_t k=0; k<sz_a; k++) a_meas[k] = (uint8_t)alice_det.measurements[k];
        for(uint32_t k=0; k<sz_b; k++) b_meas[k] = (uint8_t)bob_det.measurements[k];

        // ---------------------------------------------------------
        // PASO 1: SINCRONIZACIÓN Y CRIBADO (HLS)
        // ---------------------------------------------------------
        CorrResult res;
        uint32_t a_cnt = 0;

        // 1.1 Buscar Lag
        compute_correlation_hls(
            alice_det.time_tags.data(), a_meas.data(),
            bob_det.time_tags.data(), b_meas.data(),
            sz_a, sz_b, 350, 0, MODE_SYNC,
            a_key.data(), &a_cnt, res
        );
        int16_t lag = res.best_lag;

        // 1.2 Extraer Clave (Sifting)
        compute_correlation_hls(
            alice_det.time_tags.data(), a_meas.data(),
            bob_det.time_tags.data(), b_meas.data(),
            sz_a, sz_b, 350, lag, MODE_SIFT,
            a_key.data(), &a_cnt, res
        );

        // ---------------------------------------------------------
        // PASO 2: SIMULACIÓN DE CANAL CON RUIDO (BOB)
        // ---------------------------------------------------------
        // Copiamos la clave de Alice a Bob
        uint32_t b_cnt = a_cnt;
        for(uint32_t k=0; k<a_cnt; k++) b_key[k] = a_key[k];

        // Insertamos un ERROR artificial para probar CASCADE
        // Lo ponemos cerca del final para probar si el padding funciona bien
        if (a_cnt > 0) {
            int error_pos = (a_cnt > 10) ? (a_cnt - 1) : 0;
            b_key[error_pos] = !b_key[error_pos]; // Flip bit
        }

        std::cout << "Batch " << std::setw(2) << i << " | Size: " << std::setw(3) << a_cnt;

        // ---------------------------------------------------------
        // PASO 3: PERMUTACIÓN (SHUFFLE) - HLS
        // ---------------------------------------------------------
		// --- PASADA 1 (SEED 12345) ---
		compute_shuffle_hls(a_key.data(), a_shuf.data(), a_cnt, 12345);
		compute_shuffle_hls(b_key.data(), b_shuf.data(), b_cnt, 12345);
		// ... calcular paridad y chequear ...
		// (Aquí verás el error en el bloque X)

		// --- PASADA 2 (SEED 67890) ---
		// Cambiamos la semilla. El error debería moverse a otro bloque.
		compute_shuffle_hls(a_key.data(), a_shuf.data(), a_cnt, 67890);
		compute_shuffle_hls(b_key.data(), b_shuf.data(), b_cnt, 67890);

		// ... calculamos paridad de nuevo ...
		compute_block_parity_hls(a_shuf.data(), a_cnt, 8, a_par.data());
		compute_block_parity_hls(b_shuf.data(), b_cnt, 8, b_par.data());

        // ---------------------------------------------------------
        // PASO 4: CÁLCULO DE PARIDAD (PARITY) - HLS
        // ---------------------------------------------------------
        // Usamos bloques de 8 bits.
        // Calculamos cuántos bloques salen (redondeando hacia arriba para padding)
        uint32_t block_size = 8;
        uint32_t n_blocks = 0;
        if (a_cnt > 0) n_blocks = (a_cnt + block_size - 1) / block_size;

        compute_block_parity_hls(a_shuf.data(), a_cnt, block_size, a_par.data());
        compute_block_parity_hls(b_shuf.data(), b_cnt, block_size, b_par.data());

        // ---------------------------------------------------------
        // PASO 5: VALIDACIÓN (CASCADE CHECK)
        // ---------------------------------------------------------
        // Alice y Bob comparan sus vectores de paridad
        bool error_detected = false;
        int mismatch_block = -1;

        for(uint32_t k=0; k<n_blocks; k++) {
            if (a_par[k] != b_par[k]) {
                error_detected = true;
                mismatch_block = k;
                break; // CASCADE detecta el primer error y para ahí para corregir
            }
        }

        // Resultado Final del Batch
        if (a_cnt == 0) {
            std::cout << " | No Key Generated (High Noise/No Match)";
        }
        else if(error_detected) {
            std::cout << " | Error Det: [YES] Block " << mismatch_block << "/" << n_blocks-1
                      << " (Robustness OK)";
        } else {
            // Si insertamos un error y no lo detecta, algo va mal
            std::cout << " | Error Det: [NO] (Warning: Error masked?)";
        }
        std::cout << std::endl;
    }

    std::cout << "============================================================\n";
    return 0;
}
