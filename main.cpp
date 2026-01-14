#include <iostream>
#include <string>
#include <vector>
#include <tgbot/tgbot.h>
#include <cpr/cpr.h>
#include <nlohmann/json.hpp>

using namespace std;
using namespace TgBot;
using json = nlohmann::json;

class TelegramClient {
    Bot bot;
    string main_logic_url = "http://localhost:8000"; 

public:
    TelegramClient(string token) : bot(token) {
        
        bot.getEvents().onCommand("start", [this](Message::Ptr message) {
            bot.getApi().sendMessage(message->chat->id, "Привет! Используйте /tests для списка опросов.");
        });

        bot.getEvents().onCommand("tests", [this](Message::Ptr message) {
            InlineKeyboardMarkup::Ptr keyboard(new InlineKeyboardMarkup);
            vector<InlineKeyboardButton::Ptr> row;
            auto btn = make_shared<InlineKeyboardButton>();
            btn->text = "Пример теста";
            btn->callbackData = "start_1";
            row.push_back(btn);
            keyboard->inlineKeyboard.push_back(row);

            
            bot.getApi().sendMessage(message->chat->id, "Выберите доступный тест:", nullptr, nullptr, keyboard);
        });

        bot.getEvents().onCallbackQuery([this](CallbackQuery::Ptr query) {
            if (query->data.find("start_") == 0) {
                bot.getApi().sendMessage(query->message->chat->id, "Тест начинается! Вопрос 1: ...");
            }
            bot.getApi().answerCallbackQuery(query->id);
        });
    }

    void run() {
        cout << "Бот запущен..." << endl;
        
        TgLongPoll poll(bot);
        while (true) {
            poll.start();
        }
    }
};

int main() {
    // ВСТАВЬ СВОЙ ТОКЕН ТУТ
    string my_token = "8560525895:AAHzHjeMBBEBfEwiphdraSieC1YfF7k0QyM";

    try {
        TelegramClient client(my_token);
        client.run();
    } catch (exception& e) {
        cerr << "Ошибка: " << e.what() << endl;
    }
    return 0;
}