#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <thread>
#include <chrono>
#include <mutex> // Добавлено для безопасности
#include <tgbot/tgbot.h>
#include <cpr/cpr.h>
#include <nlohmann/json.hpp>

using namespace std;
using namespace TgBot;
using json = nlohmann::json;

struct UserState {
    bool authorized = false;
    string authToken = "";
    int currentTestId = -1;
    int currentQuestionIdx = 0;
};

class MyTelegramBot {
private:
    string api_url = "http://your-backend-api.com"; 
    Bot bot;
    map<long, UserState> user_sessions;
    mutex sessions_mutex; 

public:
    MyTelegramBot(string token) : bot(token) {
        setupHandlers();
    }

    void setupHandlers() {
        bot.getEvents().onCommand("start", [this](Message::Ptr message) {
            bot.getApi().sendMessage(message->chat->id, 
                "Привет! Авторизуйтесь: /login <код>");
        });

        bot.getEvents().onCommand("login", [this](Message::Ptr message) {
            if (message->text.length() <= 7) return;
            handleLogin(message->chat->id, message->text.substr(7));
        });

        bot.getEvents().onCommand("tests", [this](Message::Ptr message) {
            showTestList(message->chat->id);
        });

        bot.getEvents().onCallbackQuery([this](CallbackQuery::Ptr query) {
            long chatId = query->message->chat->id;
            string data = query->data;

            if (data.find("start_test_") == 0) {
                int testId = stoi(data.substr(11));
                lock_guard<mutex> lock(sessions_mutex);
                user_sessions[chatId].currentTestId = testId;
                user_sessions[chatId].currentQuestionIdx = 0;
                sendQuestion(chatId, testId, 0);
            } 
            else if (data.find("ans_") == 0) {
                handleAnswer(chatId, data);
            }
            bot.getApi().answerCallbackQuery(query->id);
        });
    }

    void handleLogin(long chatId, string code) {
        auto response = cpr::Post(cpr::Url{api_url + "/auth/login"},
                                 cpr::Body{json{{"code", code}}.dump()},
                                 cpr::Header{{"Content-Type", "application/json"}});

        try {
            if (response.status_code == 200) {
                auto data = json::parse(response.text);
                lock_guard<mutex> lock(sessions_mutex);
                user_sessions[chatId].authToken = data["token"]; 
                user_sessions[chatId].authorized = true;
                bot.getApi().sendMessage(chatId, "✅ Успешно! Жми /tests");
            } else {
                bot.getApi().sendMessage(chatId, "❌ Ошибка кода.");
            }
        } catch (...) { cerr << "Ошибка JSON в login" << endl; }
    }

    void showTestList(long chatId) {
        lock_guard<mutex> lock(sessions_mutex);
        auto& state = user_sessions[chatId];
        if (!state.authorized) {
            bot.getApi().sendMessage(chatId, "Сначала /login!");
            return;
        }

        auto response = cpr::Get(cpr::Url{api_url + "/tests"}, cpr::Bearer{state.authToken});
        try {
            if (response.status_code == 200) {
                auto tests = json::parse(response.text);
                InlineKeyboardMarkup::Ptr keyboard(new InlineKeyboardMarkup);
                for (auto& test : tests) {
                    vector<InlineKeyboardButton::Ptr> row;
                    auto btn = make_shared<InlineKeyboardButton>();
                    btn->text = test["name"].get<string>(); 
                    btn->callbackData = "start_test_" + to_string(test["id"].get<int>());
                    row.push_back(btn);
                    keyboard->inlineKeyboard.push_back(row);
                }
                bot.getApi().sendMessage(chatId, "Выберите тест:", nullptr, nullptr, keyboard);
            }
        } catch (...) { cerr << "Ошибка JSON в tests" << endl; }
    }

    void sendQuestion(long chatId, int testId, int qIdx) {
        auto& state = user_sessions[chatId];
        auto response = cpr::Get(cpr::Url{api_url + "/test/" + to_string(testId) + "/question/" + to_string(qIdx)},
                                 cpr::Bearer{state.authToken});

        try {
            if (response.status_code == 200) {
                auto qData = json::parse(response.text);
                if (qData["end_of_test"].get<bool>()) {
                    bot.getApi().sendMessage(chatId, "🏁 Тест окончен! Баллы: " + to_string(qData["score"].get<int>()));
                    state.currentTestId = -1;
                    return;
                }

                InlineKeyboardMarkup::Ptr keyboard(new InlineKeyboardMarkup);
                for (auto& ans : qData["answers"]) {
                    vector<InlineKeyboardButton::Ptr> row;
                    auto btn = make_shared<InlineKeyboardButton>();
                    btn->text = ans["text"].get<string>();
                    
                    btn->callbackData = "ans_" + to_string(testId) + "_" + to_string(qIdx) + "_" + to_string(ans["id"].get<int>());
                    row.push_back(btn);
                    keyboard->inlineKeyboard.push_back(row);
                }
                bot.getApi().sendMessage(chatId, qData["question_text"].get<string>(), nullptr, nullptr, keyboard);
            }
        } catch (...) { cerr << "Ошибка JSON в question" << endl; }
    }

    
    void handleAnswer(long chatId, string data) {
        lock_guard<mutex> lock(sessions_mutex);
        auto& state = user_sessions[chatId];

        
        cpr::Post(cpr::Url{api_url + "/test/submit_answer"},
                  cpr::Bearer{state.authToken},
                  cpr::Body{json{{"test_id", state.currentTestId}, {"q_idx", state.currentQuestionIdx}, {"ans_data", data}}.dump()},
                  cpr::Header{{"Content-Type", "application/json"}});

        state.currentQuestionIdx++;
        sendQuestion(chatId, state.currentTestId, state.currentQuestionIdx);
    }

    void startNotificationThread() {
        thread([this]() {
            while (true) {
                this_thread::sleep_for(chrono::seconds(30));
                
                
                map<long, UserState> sessions_copy;
                {
                    lock_guard<mutex> lock(sessions_mutex);
                    sessions_copy = user_sessions;
                }

                for (auto const& [chatId, state] : sessions_copy) {
                    if (!state.authorized) continue;
                    auto response = cpr::Get(cpr::Url{api_url + "/notifications"}, cpr::Bearer{state.authToken});
                    if (response.status_code == 200) {
                        try {
                            auto data = json::parse(response.text);
                            for (auto& note : data["new_messages"]) {
                                bot.getApi().sendMessage(chatId, "🔔 " + note.get<string>());
                            }
                        } catch (...) {}
                    }
                }
            }
        }).detach();
    }

    void run() {
        cout << "Бот запущен и готов к работе!" << endl;
        startNotificationThread();
        try {
            TgLongPoll poll(bot);
            while (true) poll.start();
        } catch (exception& e) { cerr << "Ошибка: " << e.what() << endl; }
    }
};

int main() {
    MyTelegramBot bot("8210319078:AAGRFW0cnVQUuRtO8lnQMAS4vgSf5mKY1TQ");
    bot.run();
    return 0;
}