#pragma once

#include <filesystem>

// Operaciones del sistema operativo para el panel Proyecto (separadas: windows.h define macros como
// near/far/min/max que chocan con el resto del editor).

// Envía el archivo o la carpeta a la papelera de reciclaje (se puede recuperar). false si falla.
bool moveToRecycleBin(const std::filesystem::path& path);
// Abre el Explorador de Windows con el archivo seleccionado.
void revealInExplorer(const std::filesystem::path& path);
