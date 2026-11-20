// Unit tests for the pieces that need no pipeline: records, schemas, the validator, and ids. Pipeline tests are in pipeline_tests.cpp.
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "sdk/data/field_value.h"
#include "sdk/data/identity.h"
#include "sdk/events/crash.h"
#include "sdk/events/join_method.h"
#include "sdk/events/login.h"
#include "sdk/events/mission_type.h"
#include "sdk/events/platform.h"
#include "sdk/events/purchased_item.h"
#include "sdk/events/squad_member.h"
#include "sdk/events/start_mission.h"
#include "sdk/ids.h"
#include "sdk/schema/field_spec.h"
#include "sdk/schema/field_type.h"
#include "sdk/schema/record_builder.h"
#include "sdk/schema/schema_builder.h"
#include "service/validator.h"
#include "standins/stub_identity_provider.h"
#include "tests/check.h"

using namespace tracking;

namespace
{

    constexpr std::size_t kUuidLength = 36;
    constexpr std::size_t kUuidVersionIndex = 14;     // the first digit of the third group
    constexpr std::size_t kUuidTimePrefixLength = 13; // the first two groups hold the timestamp
    constexpr std::size_t kSessionIdLength = 16;

    void TestRecordComesFromTheStruct()
    {
        const std::vector<std::pair<std::string, FieldValue>> fields = FieldsOf(PurchasedItem{.item = "Forma", .quantity = 3});
        CHECK(fields.size() == 2);
        CHECK(fields[0].first == "item");
        CHECK(*fields[0].second.As<std::string>() == "Forma");
        CHECK(fields[1].first == "quantity");
        CHECK(*fields[1].second.As<std::int64_t>() == 3);

        // An enum is sent by name.
        const std::vector<std::pair<std::string, FieldValue>> login = FieldsOf(Login{.platform = Platform::Steam});
        CHECK(*login[0].second.As<std::string>() == "Steam");
    }

    void TestUnsetOptionalIsLeftOut()
    {
        const std::vector<std::pair<std::string, FieldValue>> solo = FieldsOf(StartMission{.mission_type = MissionType::Defense, .node = "SolNode26"});
        CHECK(solo.size() == 2); // no "squad" key at all, not a null
        CHECK(*solo[0].second.As<std::string>() == "DEFENSE");
    }

    void TestNestedArrayOfObjects()
    {
        std::vector<SquadMember> members;
        members.push_back(SquadMember{.player_id = "p2", .join_method = JoinMethod::Party});
        const std::vector<std::pair<std::string, FieldValue>> fields = FieldsOf(StartMission{.mission_type = MissionType::Survival, .node = "SolNode26", .squad = members});
        CHECK(fields.size() == 3);
        const std::vector<FieldValue>* squad = fields[2].second.As<std::vector<FieldValue>>();
        CHECK(squad != nullptr && squad->size() == 1);
        const std::vector<std::pair<std::string, FieldValue>>* member = squad != nullptr ? (*squad)[0].As<std::vector<std::pair<std::string, FieldValue>>>() : nullptr;
        CHECK(member != nullptr && member->size() == 2);
        CHECK(member != nullptr && *(*member)[1].second.As<std::string>() == "PARTY");
    }

    void TestSchemaComesFromTheStruct()
    {
        const std::vector<FieldSpec> schema = SchemaOf<StartMission>();
        CHECK(schema.size() == 3);
        CHECK(schema[0].name == "mission_type" && schema[0].type == FieldType::String && schema[0].required);
        CHECK(schema[2].name == "squad" && schema[2].type == FieldType::Array && schema[2].required == false);
        CHECK(schema[2].children.size() == 1 && schema[2].children[0].type == FieldType::Object);
        CHECK(schema[2].children[0].children.size() == 2);

        const std::vector<FieldSpec> purchase = SchemaOf<PurchasedItem>();
        CHECK(purchase[1].name == "quantity" && purchase[1].type == FieldType::Int && purchase[1].required);
    }

    void TestValidatorAcceptsWhatTheSdkBuilds()
    {
        const std::vector<FieldSpec> login_schema = SchemaOf<Login>();
        const std::vector<std::pair<std::string, FieldValue>> login_fields = FieldsOf(Login{.platform = Platform::Epic});
        const std::optional<std::string> login_problem = Validator::Validate(login_schema, login_fields);
        CHECK(login_problem.has_value() == false);

        const std::vector<FieldSpec> purchase_schema = SchemaOf<PurchasedItem>();
        const std::vector<std::pair<std::string, FieldValue>> purchase_fields = FieldsOf(PurchasedItem{.item = "Forma", .quantity = 3});
        const std::optional<std::string> purchase_problem = Validator::Validate(purchase_schema, purchase_fields);
        CHECK(purchase_problem.has_value() == false);

        const std::vector<FieldSpec> crash_schema = SchemaOf<Crash>();
        const std::vector<std::pair<std::string, FieldValue>> crash_fields = FieldsOf(Crash{.crashed_session_id = "abc"});
        const std::optional<std::string> crash_problem = Validator::Validate(crash_schema, crash_fields);
        CHECK(crash_problem.has_value() == false);

        std::vector<SquadMember> squad;
        squad.push_back(SquadMember{.player_id = "p2", .join_method = JoinMethod::Matchmaking});
        const StartMission mission{.mission_type = MissionType::Capture, .node = "SolNode26", .squad = squad};
        const std::vector<FieldSpec> mission_schema = SchemaOf<StartMission>();
        const std::vector<std::pair<std::string, FieldValue>> mission_fields = FieldsOf(mission);
        const std::optional<std::string> mission_problem = Validator::Validate(mission_schema, mission_fields);
        CHECK(mission_problem.has_value() == false);
    }

    // What a tampered or out-of-date client could send.
    void TestValidatorRejectsBadRecords()
    {
        const std::vector<FieldSpec> purchase = SchemaOf<PurchasedItem>();
        CHECK(Validator::Validate(purchase, std::vector<std::pair<std::string, FieldValue>>{{"item", "Forma"}, {"quantity", "3"}}) == "wrong type at quantity");
        CHECK(Validator::Validate(purchase, std::vector<std::pair<std::string, FieldValue>>{{"item", "Forma"}}) == "missing required field quantity");
        CHECK(Validator::Validate(purchase, std::vector<std::pair<std::string, FieldValue>>{{"item", "Forma"}, {"quantity", 3}, {"hack", true}}) == "unknown field hack");

        const std::vector<FieldSpec> mission = SchemaOf<StartMission>();
        const std::vector<std::pair<std::string, FieldValue>> bad_member{{"mission_type", "DEFENSE"}, {"node", "SolNode26"}, {"squad", std::vector<FieldValue>{std::vector<std::pair<std::string, FieldValue>>{{"player_id", "p2"}}}}};
        CHECK(Validator::Validate(mission, bad_member) == "missing required field squad[0].join_method");
        const std::vector<std::pair<std::string, FieldValue>> bad_squad{{"mission_type", "DEFENSE"}, {"node", "SolNode26"}, {"squad", "p2"}};
        CHECK(Validator::Validate(mission, bad_squad) == "wrong type at squad");

        // A null matches no declared type, so it is refused like any other wrong type.
        const FieldValue null_value{};
        CHECK(Validator::Validate(purchase, std::vector<std::pair<std::string, FieldValue>>{{"item", "Forma"}, {"quantity", null_value}}) == "wrong type at quantity");

        // No event has a double field yet, so this schema is written by hand.
        FieldSpec ratio;
        ratio.name = "ratio";
        ratio.type = FieldType::Double;
        const std::vector<FieldSpec> with_double{ratio};
        const double not_a_number = std::numeric_limits<double>::quiet_NaN();
        const double infinity = std::numeric_limits<double>::infinity();
        CHECK(Validator::Validate(with_double, std::vector<std::pair<std::string, FieldValue>>{{"ratio", 0.5}}) == std::nullopt);
        CHECK(Validator::Validate(with_double, std::vector<std::pair<std::string, FieldValue>>{{"ratio", not_a_number}}) == "not a finite number at ratio");
        CHECK(Validator::Validate(with_double, std::vector<std::pair<std::string, FieldValue>>{{"ratio", infinity}}) == "not a finite number at ratio");
    }

    void TestIdShapes()
    {
        const std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
        const std::string first_id = NewEventId(now);
        const std::string second_id = NewEventId(now);
        CHECK(first_id.size() == kUuidLength && first_id[kUuidVersionIndex] == '7');
        CHECK(first_id != second_id);
        // Made in the same millisecond, so the time prefix matches.
        CHECK(first_id.substr(0, kUuidTimePrefixLength) == second_id.substr(0, kUuidTimePrefixLength));
    }

    void TestStubIdentityStandsInForThePlatform()
    {
        const StubIdentityProvider first_launch;
        const StubIdentityProvider second_launch;
        const Identity first_identity = first_launch.Current();
        const Identity second_identity = second_launch.Current();
        // Both ids are set before anyone signs in.
        CHECK(first_identity.session_id.size() == kSessionIdLength);
        CHECK(first_identity.install_id.size() == kUuidLength && first_identity.install_id[kUuidVersionIndex] == '4');
        CHECK(first_identity.player_id.has_value() == false);
        CHECK(first_identity.session_id != second_identity.session_id);
    }

} // namespace

int main()
{
    TestRecordComesFromTheStruct();
    TestUnsetOptionalIsLeftOut();
    TestNestedArrayOfObjects();
    TestSchemaComesFromTheStruct();
    TestValidatorAcceptsWhatTheSdkBuilds();
    TestValidatorRejectsBadRecords();
    TestIdShapes();
    TestStubIdentityStandsInForThePlatform();
    RunPipelineTests();

    std::cout << test::checks << " checks, " << test::failures << " failed\n";
    return test::failures == 0 ? 0 : 1;
}
