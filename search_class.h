#ifndef SEARCH_CLASS_H
#define SEARCH_CLASS_H

#include <iostream>
#include <string>
#include <vector>
#include <unordered_map>
#include <set>
#include <algorithm>
#include <memory>
#include <cmath>
#include <fstream>
#include <sstream>
#include <random>

using namespace std;

// Структура для хранения позиций терма в документе
struct TermPositions {
    int doc_id;
    vector<int> positions; // тут упорядоченный список

    TermPositions(int doc) : doc_id(doc) {}

    // для сортировки по doc_id
    bool operator<(const TermPositions& other) const {
        return doc_id < other.doc_id;
    }
};

// Структура для обратного индекса: терм -> упорядоченный список doc_id
using InvertedIndex = unordered_map<string, vector<int>>;

// Структура для координатного индекса: терм -> упорядоченный список (doc_id + позиции терма в доке)
using CoordinateIndex = unordered_map<string, vector<TermPositions>>;

// Структура для полей документа
struct DocumentField {
    string name;
    string content;
    vector<string> tokens;
    vector<int> positions;
};

// Структура для узла скип-листа для обратного индекса
struct SkipListNode {
    int doc_id;
    shared_ptr<SkipListNode> next;
    shared_ptr<SkipListNode> skip;

    SkipListNode(int doc) : doc_id(doc), next(nullptr), skip(nullptr) {}
};

// Типы операторов
enum class OperatorType {
    TERM, AND, OR, NOT, NEAR, ADJ
};

// Структура для узла дерева разбора запроса
struct ASTNode {
    OperatorType type;
    string value;           // Для термов
    string field;          // Для поиска по полям (если пустая строка - ищем по всем полям)
    int distance;          // Для операций NEAR и ADJ
    shared_ptr<ASTNode> left;
    shared_ptr<ASTNode> right;

    ASTNode(OperatorType t, const string& val = "", const string& fld = "", int dist = 0)
        : type(t), value(val), field(fld), distance(dist), left(nullptr), right(nullptr) {}
};



// Класс парсера запросов
class QueryParser {
private:
    vector<string> tokens;
    size_t current;

public:
    QueryParser(const string& query) : current(0) {
        tokenizeQuery(query);
    }

    shared_ptr<ASTNode> parse() {
        if (tokens.empty()) return nullptr;
        return parseOr();
    }


private:
    // токенизируем запрос
    void tokenizeQuery(const string& query) {
        string token;
        bool in_quotes = false;

        for (char c : query) {
            if (c == '"') {
                in_quotes = !in_quotes;
                if (!token.empty()) {
                    tokens.push_back(token);
                    token.clear();
                }
            } else if (isspace(c) && !in_quotes) {
                if (!token.empty()) {
                    tokens.push_back(token);
                    token.clear();
                }
            } else if ((c == '(' || c == ')' || c == '~' || c == '/') && !in_quotes) {
                if (!token.empty()) {
                    tokens.push_back(token);
                    token.clear();
                }
                tokens.push_back(string(1, c));
            } else {
                token += c;
            }
        }

        if (!token.empty()) {
            tokens.push_back(token);
        }

        // итого, у нас есть список токенов, но надо объединить в один токен структуры "NEAR / number" и "ADJ / number"
        vector<string> combined_tokens;
        for (int i = 0; i < tokens.size(); ++i) {
            if ((tokens[i] == "NEAR" || tokens[i] == "ADJ") &&
                i + 2 < tokens.size() && tokens[i + 1] == "/" &&
                !tokens[i + 2].empty() && isdigit(tokens[i + 2][0])) {

                combined_tokens.push_back(tokens[i] + "/" + tokens[i + 2]);
                i += 2;
            } else {
                combined_tokens.push_back(tokens[i]);
            }
        }
        tokens = combined_tokens;
    }

    // парсинг OR
    shared_ptr<ASTNode> parseOr() {
        auto left = parseAnd();

        while (current < tokens.size() && (tokens[current] == "OR")) {
            current++;
            auto right = parseAnd();
            auto node = make_shared<ASTNode>(OperatorType::OR);
            node->left = left;
            node->right = right;
            left = node;
        }
        return left;
    }

    // парсинг AND
    shared_ptr<ASTNode> parseAnd() {
        auto left = parseNot();

        while (current < tokens.size()) {
            // Проверяем, является ли следующий токен оператором OR, NOT или закрывающей скобкой
            if (current < tokens.size() && (tokens[current] == "OR" || tokens[current] == "NOT" || tokens[current] == ")")) {
                break;
            }

            // Проверяем, является ли следующий токен оператором NEAR/k или ADJ/k
            if (current < tokens.size() && (tokens[current].find("NEAR/") == 0 || tokens[current].find("ADJ/") == 0)) {
                break;
            }

            // Пропускаем явные операторы AND
            if (tokens[current] == "AND") {
                current++;
            }

            auto right = parseNot();
            if (!right) break;

            auto node = make_shared<ASTNode>(OperatorType::AND);
            node->left = left;
            node->right = right;
            left = node;
        }
        return left;
    }

    // парсинг NOT
    shared_ptr<ASTNode> parseNot() {
        if (current < tokens.size() && (tokens[current] == "NOT")) {
            current++;
            auto operand = parsePrimary();
            auto node = make_shared<ASTNode>(OperatorType::NOT);
            node->left = operand;
            return node;
        }
        return parsePrimary();
    }

    shared_ptr<ASTNode> parsePrimary() {
        if (current >= tokens.size()) return nullptr;

        if (tokens[current] == "(") {
            current++;
            auto node = parseOr();
            if (current < tokens.size() && tokens[current] == ")") {
                current++;
            }
            return node;
        }

        // обработаем NEAR и ADJ до проверки обычного терма
        if (current + 2 < tokens.size()) {
            string term1 = tokens[current];
            string op_with_dist = tokens[current + 1];
            string term2 = tokens[current + 2];

            // проверим, что это действительно операторы NEAR или ADJ и извлечем расстояние, которое в них указано
            if ((op_with_dist.find("NEAR/") == 0 || op_with_dist.find("ADJ/") == 0)) {
                int slash_pos = op_with_dist.find('/');
                string op = op_with_dist.substr(0, slash_pos);
                int distance = stoi(op_with_dist.substr(slash_pos + 1));
                current += 3;
                auto node = make_shared<ASTNode>(op == "NEAR" ? OperatorType::NEAR : OperatorType::ADJ, "", "", distance);

                // Обрабатываем термы с полями для левого и правого операндов
                auto left_node = parseFieldTerm(term1);
                auto right_node = parseFieldTerm(term2);
                node->left = left_node;
                node->right = right_node;
                return node;
            }
        }
        string term = tokens[current];
        current++;
        return parseFieldTerm(term);
    }

    // Парсинг терма с возможным указанием поля
    shared_ptr<ASTNode> parseFieldTerm(const string& term_str) {
        size_t colon_pos = term_str.find(':');
        
        if (colon_pos != string::npos && colon_pos > 0 && colon_pos < term_str.length() - 1) {
            // если есть указание поля, то удалим кавычки, если есть и парсим терм с указанием поля
            string field = term_str.substr(0, colon_pos);
            string term = term_str.substr(colon_pos + 1);
            if (term.length() >= 2 && term.front() == '"' && term.back() == '"') {
                term = term.substr(1, term.length() - 2);
            }
            return make_shared<ASTNode>(OperatorType::TERM, term, field);
        } else {
            // если его нет, то парсим терм с field = ""
            string term = term_str;
            if (term.length() >= 2 && term.front() == '"' && term.back() == '"') {
                term = term.substr(1, term.length() - 2);
            }
            return make_shared<ASTNode>(OperatorType::TERM, term, "");
        }
    }
};

class TextIndexer {
private:
    InvertedIndex inverted_index;
    CoordinateIndex coordinate_index;
    unordered_map<string, shared_ptr<SkipListNode>> skip_lists;

    unordered_map<int, string> doc_titles; // doc_id -> заголовок
    unordered_map<int, string> doc_contents; //doc_id -> содержание
    int next_doc_id;
    set<int> all_doc_ids;

    // Отдельные индексы для полей
    unordered_map<string, InvertedIndex> field_inverted_index; // field_name -> inverted_index
    unordered_map<string, CoordinateIndex> field_coordinate_index; // field_name -> coordinate_index

public:
    TextIndexer() : next_doc_id(1) {}

    // добавление документа с его полями
    int addDocument(const vector<pair<string, string>>& document_pairs) {
        int doc_id = next_doc_id++;
        all_doc_ids.insert(doc_id);

        string full_content;
        string title;

        // обрабатываем поля документа
        for (const auto& [field_name, text] : document_pairs) {
            DocumentField field;
            field.name = field_name;
            field.content = text;

            // токенизируем
            vector<string> tokens = tokenize(text);
            field.tokens = tokens;
            full_content += text + " ";

            if (field_name == "title") {
                title = text;
                doc_titles[doc_id] = title;
            }

            if (field_name == "content") {
                doc_contents[doc_id] = text;
            }

            // и индексируем конкретное поле
            indexField(doc_id, field_name, text);
        }

        // а тут индексируем документ целиком + не забываем отсортировать все индексы после добавления очередного дока
        indexDocumentFields(doc_id, full_content);
        sortIndexes();

        return doc_id;
    }

    // индексация конкретного поля
    void indexField(int doc_id, const string& field_name, const string& text) {
        vector<string> tokens = tokenize(text);
        unordered_map<string, vector<int>> term_positions;

        for (int pos = 0; pos < tokens.size(); ++pos) {
            string term = normalizeTerm(tokens[pos]);
            if (term.empty()) continue;
            term_positions[term].push_back(pos);
        }

        // Обновляем индексы для конкретного поля
        for (const auto& [term, positions] : term_positions) {
            // обратный
            auto& inv_list = field_inverted_index[field_name][term];
            if (find(inv_list.begin(), inv_list.end(), doc_id) == inv_list.end()) {
                inv_list.push_back(doc_id);
            }

            // координатный
            auto& coord_list = field_coordinate_index[field_name][term];
            bool doc_exists = false;
            for (auto& term_pos : coord_list) {
                if (term_pos.doc_id == doc_id) {
                    term_pos.positions.insert(term_pos.positions.end(), positions.begin(), positions.end());
                    doc_exists = true;
                    break;
                }
            }
            if (!doc_exists) {
                TermPositions term_doc(doc_id);
                term_doc.positions = positions;
                coord_list.push_back(term_doc);
            }
        }
    }

    // индексация документа целиком
    void indexDocumentFields(int doc_id, const string& full_content) {
        vector<string> tokens = tokenize(full_content);
        unordered_map<string, vector<int>> term_positions;

        for (int pos = 0; pos < tokens.size(); ++pos) {
            string term = normalizeTerm(tokens[pos]);
            if (term.empty()) continue;
            term_positions[term].push_back(pos);
        }

        // обновляем общие индексы
        for (const auto& [term, positions] : term_positions) {
            // обратный
            auto& inv_list = inverted_index[term];
            if (find(inv_list.begin(), inv_list.end(), doc_id) == inv_list.end()) {
                inv_list.push_back(doc_id);
            }

            // координатный
            auto& coord_list = coordinate_index[term];
            bool doc_exists = false;
            for (auto& term_pos : coord_list) {
                if (term_pos.doc_id == doc_id) {
                    term_pos.positions.insert(term_pos.positions.end(), positions.begin(), positions.end());
                    doc_exists = true;
                    break;
                }
            }
            if (!doc_exists) {
                TermPositions term_doc(doc_id);
                term_doc.positions = positions;
                coord_list.push_back(term_doc);
            }
        }
    }

    // выполнение сложного запроса с рекурсивным вычислением его дерева
    vector<int> executeQuery(const string& query) {
        QueryParser parser(query);
        auto ast = parser.parse();
        if (!ast) return {};
        return evaluateAST(ast);
    }

    vector<int> evaluateAST(shared_ptr<ASTNode> node) {
        if (!node) return {};

        switch (node->type) {
            case OperatorType::TERM:
                return searchTerm(node->value, node->field);

            case OperatorType::AND:
                return executeAND(evaluateAST(node->left), evaluateAST(node->right));

            case OperatorType::OR:
                return executeOR(evaluateAST(node->left), evaluateAST(node->right));

            case OperatorType::NOT:
                return executeNOT(evaluateAST(node->left));

            case OperatorType::NEAR:
                return executeProximityQuery(
                    node->left->value, node->right->value,
                    node->left->field, node->right->field,
                    node->distance, false);

            case OperatorType::ADJ:
                return executeProximityQuery(
                    node->left->value, node->right->value,
                    node->left->field, node->right->field,
                    node->distance, true);

            default:
                return {};
        }
    }

    // Базовые операции

    // Поиск по одному терму с учетом поля
    vector<int> searchTerm(const string& term, const string& field = "") {
        string normalized_term = normalizeTerm(term);

        // если field пустое, то ищем по всем полям, иначе - в конкретном
        if (!field.empty()) {
            auto field_it = field_inverted_index.find(field);
            if (field_it != field_inverted_index.end()) {
                auto term_it = field_it->second.find(normalized_term);
                return term_it != field_it->second.end() ? term_it->second : vector<int>();
            }
            return vector<int>();
        } else {
            auto it = inverted_index.find(normalized_term);
            return it != inverted_index.end() ? it->second : vector<int>();
        }
    }

    // Операция AND
    vector<int> executeAND(const vector<int>& list1, const vector<int>& list2) {
        vector<int> result;
        if (list1.empty() || list2.empty()) return result;

        int i = 0, j = 0;
        while (i < list1.size() && j < list2.size()) {
            if (list1[i] == list2[j]) {
                result.push_back(list1[i]);
                i++; j++;
            } else if (list1[i] < list2[j]) {
                i++;
            } else {
                j++;
            }
        }
        return result;
    }

    // Операция OR
    vector<int> executeOR(const vector<int>& list1, const vector<int>& list2) {
        if (list1.empty()) return list2;
        if (list2.empty()) return list1;

        vector<int> result;
        int i = 0, j = 0;

        while (i < list1.size() && j < list2.size()) {
            if (list1[i] < list2[j]) {
                result.push_back(list1[i++]);
            } else if (list1[i] > list2[j]) {
                result.push_back(list2[j++]);
            } else {
                result.push_back(list1[i++]);
                j++;
            }
        }

        while (i < list1.size()) result.push_back(list1[i++]);
        while (j < list2.size()) result.push_back(list2[j++]);

        return result;
    }

    // Операция NOT
    vector<int> executeNOT(const vector<int>& list) {
        vector<int> result;

        for (int doc_id : all_doc_ids) {
            if (!binary_search(list.begin(), list.end(), doc_id)) {
                result.push_back(doc_id);
            }
        }
        return result;
    }

    // поиск с ограничением расстояния между термами для NEAR и ADJ
    vector<int> executeProximityQuery(const string& term1, const string& term2,
                                     const string& field1, const string& field2,
                                     int max_distance, bool adjacent_only = false) {
        vector<int> results;

        string norm_term1 = normalizeTerm(term1);
        string norm_term2 = normalizeTerm(term2);

        // получаем списки позиций с учетом полей
        const vector<TermPositions>* list1_ptr = nullptr;
        const vector<TermPositions>* list2_ptr = nullptr;

        if (field1.empty()) {
            auto it1 = coordinate_index.find(norm_term1);
            if (it1 != coordinate_index.end()) list1_ptr = &it1->second;
        } else {
            auto field_it = field_coordinate_index.find(field1);
            if (field_it != field_coordinate_index.end()) {
                auto it1 = field_it->second.find(norm_term1);
                if (it1 != field_it->second.end()) list1_ptr = &it1->second;
            }
        }

        if (field2.empty()) {
            auto it2 = coordinate_index.find(norm_term2);
            if (it2 != coordinate_index.end()) list2_ptr = &it2->second;
        } else {
            auto field_it = field_coordinate_index.find(field2);
            if (field_it != field_coordinate_index.end()) {
                auto it2 = field_it->second.find(norm_term2);
                if (it2 != field_it->second.end()) list2_ptr = &it2->second;
            }
        }

        if (!list1_ptr || !list2_ptr) {
            return results;
        }

        const auto& list1 = *list1_ptr;
        const auto& list2 = *list2_ptr;

        int i = 0, j = 0;
        while (i < list1.size() && j < list2.size()) {
            if (list1[i].doc_id == list2[j].doc_id) {
                if (adjacent_only ?
                    hasAdjacentPositions(list1[i].positions, list2[j].positions, max_distance) :
                    hasClosePositions(list1[i].positions, list2[j].positions, max_distance)) {
                    results.push_back(list1[i].doc_id);
                }
                i++; j++;
            } else if (list1[i].doc_id < list2[j].doc_id) {
                i++;
            } else {
                j++;
            }
        }

        return results;
    }

    // методы для получения заголовка и содержания дока
    string getDocumentTitle(int doc_id) {
        auto it = doc_titles.find(doc_id);
        return it != doc_titles.end() ? it->second : "Document " + to_string(doc_id);
    }

    string getDocumentContent(int doc_id) {
        auto it = doc_contents.find(doc_id);
        return it != doc_contents.end() ? it->second : "";
    }

private:
    // Вспомогательные методы

    // токенизация
    vector<string> tokenize(const string& text) {
        vector<string> tokens;
        string token;

        for (char c : text) {
            if (isspace(c) || c == '.' || c == ',' || c == '!' || c == '?' || c == ';' || c == ':') {
                if (!token.empty()) {
                    tokens.push_back(token);
                    token.clear();
                }
            } else {
                token += c;
            }
        }

        if (!token.empty()) {
            tokens.push_back(token);
        }

        return tokens;
    }

    // нормализация терма
    string normalizeTerm(const string& term) {
        string result;

        for (char c : term) {
            if (isalnum(c)) {
                result += tolower(c);
            }
        }

        return result;
    }

    // сортировка индексов для поддержания структуры + удаление возможных дублей
    void sortIndexes() {
        // обратный индекс
        for (auto& [term, doc_list] : inverted_index) {
            sort(doc_list.begin(), doc_list.end());
            auto last = unique(doc_list.begin(), doc_list.end());
            doc_list.erase(last, doc_list.end());
        }

        // координатный индекс
        for (auto& [term, doc_positions] : coordinate_index) {
            sort(doc_positions.begin(), doc_positions.end());
            for (auto& term_pos : doc_positions) {
                sort(term_pos.positions.begin(), term_pos.positions.end());
                auto last = unique(term_pos.positions.begin(), term_pos.positions.end());
                term_pos.positions.erase(last, term_pos.positions.end());
            }
        }

        // и такие же индексы для полей
        for (auto& [field_name, field_index] : field_inverted_index) {
            for (auto& [term, doc_list] : field_index) {
                sort(doc_list.begin(), doc_list.end());
                auto last = unique(doc_list.begin(), doc_list.end());
                doc_list.erase(last, doc_list.end());
            }
        }

        for (auto& [field_name, coord_index] : field_coordinate_index) {
            for (auto& [term, doc_positions] : coord_index) {
                sort(doc_positions.begin(), doc_positions.end());
                for (auto& term_pos : doc_positions) {
                    sort(term_pos.positions.begin(), term_pos.positions.end());
                    auto last = unique(term_pos.positions.begin(), term_pos.positions.end());
                    term_pos.positions.erase(last, term_pos.positions.end());
                }
            }
        }

        buildSkipLists();
    }

    // построение скип-листов с шагом sqrt(n)
    void buildSkipLists() {
        skip_lists.clear();
        for (const auto& [term, doc_list] : inverted_index) {
            if (doc_list.empty()) continue;
            auto head = make_shared<SkipListNode>(doc_list[0]);
            auto current = head;
            for (int i = 1; i < doc_list.size(); ++i) {
                auto new_node = make_shared<SkipListNode>(doc_list[i]);
                current->next = new_node;
                current = new_node;
            }
            addSkipPointers(head, doc_list.size());
            skip_lists[term] = head;
        }
    }

    void addSkipPointers(shared_ptr<SkipListNode> head, int list_size) {
        if (!head || list_size < 3) return;
        int skip_step = static_cast<int>(sqrt(list_size));
        auto current = head;
        int count = 0;
        while (current) {
            if (count % skip_step == 0) {
                auto skip_target = current;
                for (int i = 0; i < skip_step && skip_target; i++) {
                    skip_target = skip_target->next;
                }
                current->skip = skip_target;
            }
            current = current->next;
            count++;
        }
    }

    // Проверка близости позиций для реализации оператора NEAR
    // (пользуемся тем, что все упорядочено)
    bool hasClosePositions(const vector<int>& positions1, const vector<int>& positions2, int max_distance) {
        int i = 0, j = 0;
        while (i < positions1.size() && j < positions2.size()) {
            int distance = abs(positions1[i] - positions2[j]);
            if (distance <= max_distance) {
                return true;
            }
            if (positions1[i] < positions2[j]) {
                i++;
            } else {
                j++;
            }
        }
        return false;
    }

    // Проверка соседства позиций для реализации оператора ADJ
    // (пользуемся тем, что все упорядочено)
    bool hasAdjacentPositions(const vector<int>& positions1, const vector<int>& positions2, int max_distance) {
        int i = 0, j = 0;
        while (i < positions1.size() && j < positions2.size()) {
            int distance = positions2[j] - positions1[i];
            if (distance > 0 && distance <= max_distance) {
                return true;
            }
            if (positions1[i] < positions2[j]) {
                i++;
            } else {
                j++;
            }
        }
        return false;
    }
};

#endif