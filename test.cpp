#include "search_class.h"
#include <vector>
#include <string>
#include <sstream>
#include <iostream>
#include <fstream>
#include <map>
#include <chrono>

using namespace std;
using namespace chrono;

// Структура для хранения доков (поля id, title, content)
struct Document {
    int id;
    map<string, string> fields;

    // достаем заголовок и содержание
    string getTitle() {
        auto it = fields.find("title");
        return it != fields.end() ? it->second : "";
    }
    string getContent() {
        auto it = fields.find("content");
        return it != fields.end() ? it->second : "";
    }

    // проверка на пустоту дока
    bool isEmpty() {
        return getTitle().empty() && getContent().empty();
    }
};

// Функция для парсинга CSV файла с данными + ограничение по кол-ву строк
vector<Document> parseCSV(string& filename, int max_rows = 100, char sep = ',') {
    vector<Document> documents;
    ifstream file(filename);
    string line;
    vector<string> headers;
    int line_number = 0;
    int rows_loaded = 0;

    // Читаем и парсим первую строку с названиями полей
    if (getline(file, line)) {
        line_number++;
        stringstream header_ss(line);
        string header;
        while (getline(header_ss, header, sep)) {
            header.erase(0, header.find_first_not_of(" \t"));
            header.erase(header.find_last_not_of(" \t") + 1);
            if (header.length() >= 2 && header[0] == '"' && header.back() == '"') {
                header = header.substr(1, header.length() - 2);
            }
            headers.push_back(header);
        }
    }

    // парсим данные с ограничением по числу строк
    while (getline(file, line) && rows_loaded < max_rows) {
        line_number++;
        rows_loaded++;

        Document doc;
        vector<string> fields;
        bool in_quotes = false;
        string current_field;

        if (line.empty()) continue;

        for (char c : line) {
            if (c == '"') {
                in_quotes = !in_quotes;
            } else if (c == ',' && !in_quotes) {
                fields.push_back(current_field);
                current_field.clear();
            } else {
                current_field += c;
            }
        }
        fields.push_back(current_field);

        // обрабатываем поля
        for (size_t i = 0; i < min(headers.size(), fields.size()); ++i) {
            string field_value = fields[i];
            field_value.erase(0, field_value.find_first_not_of(" \t"));
            field_value.erase(field_value.find_last_not_of(" \t") + 1);
            if (field_value.length() >= 2 && field_value[0] == '"' && field_value.back() == '"') {
                field_value = field_value.substr(1, field_value.length() - 2);
            }

            // Обрабатываем поле id отдельно, если он невалиден - используем номер строки в файле
            if (headers[i] == "id") {
                try {
                    doc.id = stoi(field_value);
                } catch (exception& e) {
                    doc.id = line_number;
                }
            }
            else if (headers[i] == "content" || headers[i] == "title") {
                doc.fields[headers[i]] = field_value;
            }
        }
        // Пропускаем пустые доки
        if (doc.isEmpty()) continue;
        documents.push_back(doc);
    }
    file.close();
    return documents;
}

// Функция для индексации документов
void indexDocuments(TextIndexer& indexer, vector<Document>& documents) {
    int indexed_count = 0;
    for (auto& doc : documents) {
        vector<pair<string, string>> doc_fields;

        // достаем только заголовок и содержание
        doc_fields.push_back({"title", doc.getTitle()});
        doc_fields.push_back({"content", doc.getContent()});

        int doc_id = indexer.addDocument(doc_fields);
        indexed_count++;

        // (для отладки) следим за индексацией
        if (indexed_count % 100 == 0) {
            cout << "Indexed " << indexed_count << " docs" << endl;
        }
    }
}

// Функция для вывода результатов поиска
void displaySearchResults(vector<int>& results, TextIndexer& indexer) {
    if (results.empty()) {
        cout << "Nothing found." << endl;
        return;
    }

    cout << "Found docs: " << results.size() << endl;
    int documents_to_show = min(5, (int)results.size());

    for (int i = 0; i < documents_to_show; ++i) {
        int doc_id = results[i];

        // Достаем заголовок и содержание
        string title = indexer.getDocumentTitle(doc_id);
        string content = indexer.getDocumentContent(doc_id);

        if (title.empty()) title = "No title";
        if (content.empty()) content = "No content";

        // Выводим id дока в коллекции, заголовок и обрезанное содержание до 200 символов
        cout << "[" << doc_id << "] " << title << endl;
        if (content.length() > 200) {
            cout << "\t" << content.substr(0, 200) + "..." << endl;
        } else {
            cout << "\t" << content << endl;
        }
        cout << endl;
    }
}


int main() {
    // Загружаем доки
    vector<Document> documents;
    string filename = "clear_news_no_dups.csv";
    documents = parseCSV(filename, 10000);

    if (!documents.empty()) {
        cout << "successful download" << endl;
    }

    // Индексируем доки
    TextIndexer indexer;
    indexDocuments(indexer, documents);

    // Ищем по докам
    string query;
    cout << "Total docs: " << documents.size() << endl;
    cout << "Available operations: AND, NOT, OR, NEAR/k, ADJ/k, search in fields" << endl;
    cout << "Type 'exit' to end\n" << endl;

    while (true) {
        cout << "Search request: ";
        getline(cin, query);

        if (query == "exit") break;
        if (query.empty()) continue;

        auto start_time = high_resolution_clock::now();
        vector<int> results = indexer.executeQuery(query);
        auto end_time = high_resolution_clock::now();
        cout << "Execution time: " << duration_cast<milliseconds>(end_time - start_time).count() << " ms" << endl;

        displaySearchResults(results, indexer);

        cout << "\n" << string(50, '=') << "\n" << endl;
    }
    return 0;
}