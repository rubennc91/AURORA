#include <iostream>
#include <vector>
#include <string>
#include <fstream>
#include <filesystem>

// Estructura mínima para validar que el sistema funciona
int main() {
    std::cout << "--- AURORA Environment Test ---" << std::endl;
    
    std::vector<uint64_t> test_vector = {1000, 2000, 3000};
    std::cout << "Vector size: " << test_vector.size() << std::endl;
    
    // Comprobar si vemos la carpeta de datos
    if (std::filesystem::exists("batches")) {
        std::cout << "Folder 'batches' found!" << std::endl;
    } else {
        std::cout << "Folder 'batches' NOT found. Check your directory." << std::endl;
    }

    return 0;
}