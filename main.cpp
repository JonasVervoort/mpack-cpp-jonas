#include <atomic>
#include <iostream>
#include <string>
#include <array>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <variant>
#include <vector>
#include <optional>
#include <map>
#include <cstdint>
#include "mpack/mpack.h"
#include "include/mpack_serialize_typehandlers.h"
#include "include/mpack_serializer.h"


class X90IO : public MsgPackSerializable<X90IO> {
  public:
  std::string name;
  std::variant<bool,double> data;
  static constexpr auto get_fields()
  {
    using namespace serialization;
    return std::make_tuple(
      make_field("name", &X90IO::name),
      make_field("data", &X90IO::data)
    );
  }
  void print() const
  {
    std::cout << "          Name: " << name ;
    if (std::holds_alternative<bool>(data)) {
      std::string bool_str = std::get<bool>(data) ? "TRUE" : "FALSE";
      std::cout << "  Data: " << bool_str << '\n';
    } else if (std::holds_alternative<double>(data)) {
      std::cout << "  Data: " << std::get<double>(data) << '\n';
    } else {
      // Handle unexpected type
      std::cout << "  Data: Unknown type\n";
    }
  }
};

class X90IOGroup : public MsgPackSerializable<X90IOGroup> {
  public:
  std::string name;
  std::uint64_t time_recorded;
  bool is_fail;
  std::vector<X90IO> ios; 
  static constexpr auto get_fields()
  {
    using namespace serialization;
    return std::make_tuple(
      make_field("Name", &X90IOGroup::name),
      make_field("TimeRecorded", &X90IOGroup::time_recorded),
      make_field("Fail", &X90IOGroup::is_fail),
      make_field("IOs", &X90IOGroup::ios)
    );
  }
  void print() const
  {
    std::cout << "      Name: " << name << '\n';
    std::cout << "      Time Recorded: " << time_recorded << '\n';
    std::cout << "      Fail: " << (is_fail ? "true" : "false") << '\n';
    std::cout << "      IOs:[\n";
    for (const auto & io : ios) {
      io.print();
    }
    std::cout << "      ]\n";
  }
};

class X90Msg : public MsgPackSerializable<X90Msg> {
  public:
  std::string endpoint_id;
  std::uint64_t current_time;
  std::vector<X90IOGroup> io_groups; 
  
  static constexpr auto get_fields()
  {
    using namespace serialization;
    return std::make_tuple(
      make_field("EndpointId", &X90Msg::endpoint_id),
      make_field("CurrentTime", &X90Msg::current_time),
      make_field("IOGroups", &X90Msg::io_groups)
    );
  }
  void print() const
  {
    std::cout << "  Endpoint ID: " << endpoint_id << '\n';
    std::cout << "  Current Time: " << current_time << '\n';
    std::cout << "  IOGroups: [\n" ;
    for (const auto & group : io_groups) {
      group.print();
    }
    std::cout << "  ]\n";
  }
};



int main()
{
  // Create an X90Msg instance
  X90Msg x90_msg;
  x90_msg.endpoint_id = "Endpoint123";
  x90_msg.current_time = 1622547800;
  X90IO io1;
  io1.name = "IO1";
  io1.data = true;
  X90IO io2;
  io2.name = "IO2";
  io2.data = 200.0;
  X90IOGroup group1;
  group1.name = "Group1";
  group1.time_recorded = 1622547800;
  group1.is_fail = false;
  group1.ios.push_back(io1);
  group1.ios.push_back(io2);
  x90_msg.io_groups.push_back(group1);

  std::cout << "\nOriginal X90Msg:\n";
  std::cout << "---------------\n";
  x90_msg.print();

  // Serialize to MessagePack buffer
  constexpr size_t buffer_size = 1024;
  std::array<char, buffer_size> x90_buffer;
  x90_buffer.fill(0);
  const auto size_of_x90_msg = Serializable::to_msgpack(x90_buffer, x90_msg);
  std::cout << "Serialized X90Msg to " << std::to_string(size_of_x90_msg) << " bytes\n";
  // Deserialize from buffer
  X90Msg restored_x90_msg;
  Serializable::from_msgpack(x90_buffer, restored_x90_msg);
  std::cout << "\nDeserialized X90Msg:\n";
  std::cout << "--------------------\n";
  restored_x90_msg.print();







  return 0;
}
