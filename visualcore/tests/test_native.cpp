#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <string>
#include <vector>
#include "vc/json.hpp"
#include "vc/native.h"

using vc::Id;
using vc::Node;
using vc::OpBody;

namespace {

struct Closer {
    void operator()(vc_document* document) const { vc_document_close(document); }
};
using Handle = std::unique_ptr<vc_document, Closer>;

/// Takes ownership of a string the library returned.
std::string take(char* text) {
    REQUIRE(text != nullptr);
    std::string out = text;
    vc_free(text);
    return out;
}

std::string opsJson(const std::vector<OpBody>& ops) {
    std::string out = "[";
    for (const OpBody& op : ops) {
        if (out.size() > 1) out += ',';
        out += R"({"kind":")" + std::string{vc::opKind(op)} + R"(","data":)" + vc::opToJson(op) + "}";
    }
    return out + "]";
}

Node frame() {
    Node n;
    n.id = *Id::parse("0192a4b1-7c3d-4e5f-8a6b-000000000100");
    n.type = vc::NodeType::Frame;
    n.order = "V";
    n.name = "Card";
    n.w = 200;
    n.h = 100;
    return n;
}

} // namespace

TEST_CASE("the native library applies ops and reports the ones it refuses") {
    Handle doc{vc_document_open(nullptr, nullptr)};
    REQUIRE(doc);

    Node orphan;
    orphan.id = *Id::parse("0192a4b1-7c3d-4e5f-8a6b-000000000101");
    orphan.parent = *Id::parse("0192a4b1-7c3d-4e5f-8a6b-0000000001ff");
    orphan.order = "V";
    const std::string result = take(vc_document_apply(doc.get(), opsJson({vc::CreateNode{frame()}, vc::CreateNode{orphan}}).c_str()));
    REQUIRE(result == R"([true,{"path":"ops[1]","message":"the parent does not exist or cannot have children"}])");
    REQUIRE(take(vc_last_error()) == R"({"path":"ops[1]","message":"the parent does not exist or cannot have children"})");
    REQUIRE(take(vc_document_apply(doc.get(), "[]")) == "[]");
    REQUIRE(vc_last_error() == nullptr);
    REQUIRE(vc_document_apply(doc.get(), "not json") == nullptr);
    REQUIRE(vc_last_error() != nullptr);
}

TEST_CASE("opening a snapshot explains the first layer it refuses") {
    const std::string snapshot = R"({"version":2,"modes":[],"tokens":[],"nodes":[
        {"id":"0192a4b1-7c3d-4e5f-8a6b-000000000100","type":"frame","order":"V","w":200,"h":100},
        {"id":"0192a4b1-7c3d-4e5f-8a6b-000000000103","type":"rect","order":"W","parent":"0192a4b1-7c3d-4e5f-8a6b-0000000001ff"}]})";
    int rejected = 0;
    Handle doc{vc_document_open(snapshot.c_str(), &rejected)};
    REQUIRE(doc);
    REQUIRE(rejected == 1);
    REQUIRE(take(vc_last_error()) ==
            R"({"path":"nodes[0192a4b1-7c3d-4e5f-8a6b-000000000103]","message":"the parent does not exist or cannot have children"})");
}

TEST_CASE("a native document round-trips through its snapshot") {
    Handle doc{vc_document_open("", nullptr)};
    take(vc_document_apply(doc.get(), opsJson({vc::CreateNode{frame()}}).c_str()));
    const std::string snapshot = take(vc_document_snapshot(doc.get()));

    int rejected = -1;
    Handle copy{vc_document_open(snapshot.c_str(), &rejected)};
    REQUIRE(copy);
    REQUIRE(rejected == 0);
    REQUIRE(take(vc_document_snapshot(copy.get())) == snapshot);
    REQUIRE(vc_document_open("{}", nullptr) == nullptr);
}

TEST_CASE("native queries answer from the laid-out document") {
    Handle doc{vc_document_open(nullptr, nullptr)};
    take(vc_document_apply(doc.get(), opsJson({vc::CreateNode{frame()}}).c_str()));

    const std::string node = take(vc_document_node(doc.get(), frame().id.toString().c_str(), nullptr));
    REQUIRE(node.find(R"("box":{"h":100.0,"w":200.0,"x":0.0,"y":0.0})") != std::string::npos);
    REQUIRE(take(vc_document_layers(doc.get())).find(R"("name":"Card")") != std::string::npos);
    REQUIRE(take(vc_document_tokens(doc.get(), "")) == "[]");
    REQUIRE(take(vc_document_modes(doc.get())).find(R"("isDefault":true)") != std::string::npos);

    REQUIRE(vc_document_node(doc.get(), "0192a4b1-7c3d-4e5f-8a6b-0000000001ff", nullptr) == nullptr);
    REQUIRE(vc_document_tokens(doc.get(), "0192a4b1-7c3d-4e5f-8a6b-0000000001ff") == nullptr);
}
