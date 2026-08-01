#include "invisible.h"
#include <fstream>
#include <iostream>
#include <sstream>

// static std::vector<invisible::InlineEntry> marshal_entries(const py::list& py_entries) {
//     std::vector<invisible::InlineEntry> entries;
//     entries.reserve(py_entries.size());
//     for (const auto& item : py_entries) {
//         auto dict = item.cast<py::dict>();
//         invisible::InlineEntry entry;
//         entry.caller = dict["caller"].cast<std::string>();
//         entry.callee = dict["callee"].cast<std::string>();
//         entry.line = dict["line"].cast<size_t>();
//         entry.file = dict["file"].cast<std::string>();
//         entries.push_back(std::move(entry));
//     }
//     return entries;
// }

static std::vector<invisible::InlineEntry> marshal_entries(const std::string& filepath) {
    std::vector<invisible::InlineEntry> entries;
    std::ifstream infile(filepath);
    if (!infile.is_open()) {
        std::cout << "Failed to open file: " << filepath << std::endl;
    }

    std::string line;
    while (std::getline(infile, line)) {
        if (line.empty()) continue;

        std::istringstream iss(line);
        invisible::InlineEntry entry;
        std::string line_num;
        iss >> entry.caller >> entry.callee >> line_num >> entry.file;

        entry.line = std::stoull(line_num);
        entries.push_back(std::move(entry));
    }

    return entries;
}

int main(int argc, char* argv[]) {
    const std::string pass(argv[1]);
    const std::string output_file = "output.cc";
    std::ifstream infile(argv[2]);
    std::string line;
    std::string source;
    while(std::getline(infile, line)) {
        source += std::move(line);
        source += "\n";
    }
    
    std::vector<std::string> args;
    for(int i = 3; i < argc; i++) args.emplace_back(argv[i]);
    if(pass == "preprocess") {
        std::string result = invisible::preprocess(source, output_file, args);
        std::ofstream out("a_couple_passes.cpp");
        out << result;
        out.close();
    } else if(pass == "constant-fold") {
        llvm::outs() << invisible::constant_fold(source, output_file, args) << "\n";
    } else if (pass == "dce") {
        llvm::outs() << invisible::deadCodeElim(source, output_file, args) << "\n";
    } else if(pass == "combine") {
        std::string half = invisible::constant_fold(source, output_file, args);
        std::string full = invisible::preprocess(half, output_file, args);
        std::ofstream out("output.cpp");
        out << full;
        out.close();
    } else if(pass == "inline") {
        std::string oracle_path("oracle_list.txt");
        std::vector<invisible::InlineEntry> oracle_entries =  marshal_entries(oracle_path);
        std::string result = invisible::inline_functions(source, output_file, oracle_entries, args).source;
        std::ofstream out("inlined.cpp");
        out << result;
        out.close();
    }
}

// PYBIND11_MODULE(_invisible, m) {
//     m.doc() = "invisible: C/C++ AST transformation passes";

//     m.def("preprocess", &invisible::preprocess, py::arg("source"), py::arg("filename"),
//           "Split comma declarations and hoist nested function calls. Returns the transformed source.");

//     m.def("separate_functions", &invisible::separate_functions, py::arg("source"), py::arg("filename"),
//           "Extract function definitions and return them as a JSON string.");

//     m.def("constant_fold", &invisible::constant_fold, py::arg("source"), py::arg("filename"),
//           "constant fold mathematical expressions into their simplified form.");

//     m.def(
//         "inline_functions",
//         [](const std::string& source, const std::string& filename, const py::list& py_entries) {
//             auto entries = marshal_entries(py_entries);
//             auto result = invisible::inline_functions(source, filename, entries);
//             return py::make_tuple(result.source, result.succeeded);
//         },
//         py::arg("source"), py::arg("filename"), py::arg("entries"),
//         "Inline function calls at the sites described by oracle entries. "
//         "Returns (transformed_source, per_entry_success_list).");
// }
