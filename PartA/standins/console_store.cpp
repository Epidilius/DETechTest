#include "standins/console_store.h"

#include <chrono>
#include <cstdint>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "sdk/data/event_record.h"
#include "sdk/data/field_value.h"

namespace tracking
{
    namespace
    {
        void PrintValue(std::ostream& out, const FieldValue& value);

        void PrintObject(std::ostream& out, const std::vector<std::pair<std::string, FieldValue>>& fields)
        {
            out << '{';
            bool is_first = true;
            for (const auto& [name, value] : fields)
            {
                if (is_first == false)
                {
                    out << ", ";
                }
                is_first = false;
                out << name << ": ";
                PrintValue(out, value);
            }
            out << '}';
        }

        void PrintArray(std::ostream& out, const std::vector<FieldValue>& items)
        {
            out << '[';
            bool is_first = true;
            for (const FieldValue& item : items)
            {
                if (is_first == false)
                {
                    out << ", ";
                }
                is_first = false;
                PrintValue(out, item);
            }
            out << ']';
        }

        // Plain text, for reading. Nothing is escaped.
        struct ValuePrinter
        {
            std::ostream& out;

            void operator()(std::monostate) const
            {
                out << "null";
            }
            void operator()(bool flag) const
            {
                out << (flag ? "true" : "false");
            }
            void operator()(std::int64_t number) const
            {
                out << number;
            }
            void operator()(double number) const
            {
                out << number;
            }
            void operator()(const std::string& text) const
            {
                out << '"' << text << '"';
            }
            void operator()(const std::vector<FieldValue>& items) const
            {
                PrintArray(out, items);
            }
            void operator()(const std::vector<std::pair<std::string, FieldValue>>& fields) const
            {
                PrintObject(out, fields);
            }
        };

        void PrintValue(std::ostream& out, const FieldValue& value)
        {
            const ValuePrinter printer{out};
            std::visit(printer, value.variant());
        }

        long long MillisecondsSinceEpoch(std::chrono::system_clock::time_point time)
        {
            const std::chrono::milliseconds since_epoch = std::chrono::duration_cast<std::chrono::milliseconds>(time.time_since_epoch());
            return since_epoch.count();
        }

        // One line per stored event: the envelope, then the event's own fields.
        void PrintEvent(std::ostream& out, const AcceptedEvent& event)
        {
            const EventRecord& record = event.record;
            const std::string player = record.player_id.has_value() ? *record.player_id : "none";
            const long long time_ms = MillisecondsSinceEpoch(record.time);
            const long long received_ms = MillisecondsSinceEpoch(event.received_at);
            out << record.name << " v" << record.version;
            out << " event_id=" << record.event_id;
            out << " time_ms=" << time_ms;
            out << " received_ms=" << received_ms;
            out << " session=" << record.session_id;
            out << " install=" << record.install_id;
            out << " player=" << player;
            out << " authenticated=" << (event.authenticated ? "true" : "false");
            out << " fields=";
            PrintObject(out, record.fields);
            out << '\n';
        }

    } // namespace

    InsertResult ConsoleStore::Insert(std::span<const AcceptedEvent> events)
    {
        std::lock_guard lock(mutex_);
        InsertResult result;
        for (const AcceptedEvent& event : events)
        {
            if (event_ids_.insert(event.record.event_id).second == false)
            {
                ++result.duplicates; // a retry delivered this event again
                continue;
            }
            PrintEvent(out_, event);
            ++counts_[event.record.name];
            ++result.stored;
        }
        return result;
    }

} // namespace tracking
