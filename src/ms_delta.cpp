/*
*   MIT License
*
*   Copyright (c) 2026 Pawel Drzycimski
*
*   Permission is hereby granted, free of charge, to any person obtaining a copy
*   of this software and associated documentation files (the "Software"), to deal
*   in the Software without restriction, including without limitation the rights
*   to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
*   copies of the Software, and to permit persons to whom the Software is
*   furnished to do so, subject to the following conditions:
*
*   The above copyright notice and this permission notice shall be included in all
*   copies or substantial portions of the Software.
*
*   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
*   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
*   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
*   AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
*   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
*   OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
*   SOFTWARE.
*/

#include <optional>
#include <filesystem>
#include <string>
#include <vector>
#include <iostream>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <errhandlingapi.h>
#include <msdelta.h>

namespace fs = std::filesystem;

static constexpr std::string_view DEFAULT_PATCH_EXTENSION = ".patch";

struct Args
{
  enum class Command
  {
    Create,
    Apply
  } cmd;

  std::string source, target, patches_folder, patch_file_extension { DEFAULT_PATCH_EXTENSION };
};

namespace {

void usage()
{
  std::cout << 
    "\nCreates delta diffs between name matching files in two folders."
    "\nGeneral usage:\n"
    "\tns_delta_patches <command> -s <source> -t <target> -p <patches>\n\n"
    "\t<command> - create/apply\n"
    "\t-s,--source    - source catalog path REQUIRED\n"
    "\t-t,--target    - target catalog path REQUIRED\n"
    "\t-p,--patches   - patches catalog path REQUIRED\n"
    "\t-e,--extension - patches file extension, \"" << DEFAULT_PATCH_EXTENSION << "\" by default OPTIONAL\n"
    "Example create:\n"
    "\tms_delta create -s \"C:\\source_data\" -t \"C:\\target_data\" -p \"C:\\patches\"\n\n"
    "Example apply:\n"
    "\tms_delta apply -s \"C:\\source_data\" -t \"C:\\applied_data_patches\" -p \"C:\\patches\"\n\n"
    ;
}

template<typename ...Args>
bool one_of(std::string_view str, Args&& ...args)
{
  for(const auto &arg : { std::forward<Args>(args)... }) {
    if(str == arg)
      return true;
  }

  return false;
}

std::optional<Args> process_args(int argc, char *argv[])
{
  if(argc < 2) {
    return std::nullopt;
  }

  Args ret;
  const std::string_view cmd{ argv[1] };
  if(cmd == "create")
    ret.cmd = Args::Command::Create;
  else if(cmd == "apply")
    ret.cmd = Args::Command::Apply;
  else {
    std::cout << "Invalid command provided [" << cmd << "] available - create/apply\n";
    return std::nullopt;
  }

  for(size_t i = 2; i < static_cast<size_t>(argc); ++i) {
    const std::string_view arg{ argv[i] };
    if(one_of(arg, "-s", "--source") && i + 1 < argc)
      ret.source = argv[++i];
    else if(one_of(arg, "-t", "--target") && i + 1 < argc)
      ret.target = argv[++i];
    else if(one_of(arg, "-p", "--patches") && i + 1 < argc)
      ret.patches_folder = argv[++i];
    else if(one_of(arg, "-e", "--extension") && i + 1 < argc)
      ret.patch_file_extension = argv[++i];
  }

  bool ok = true;
  if(ret.source.empty()) {
    std::cout << "Missing -s argument\n";
    ok = false;
  }

  if(ret.target.empty()) {
    std::cout << "Missing -t argument\n";
    ok = false;
  }

  if(ret.patches_folder.empty()) {
    std::cout << "Missing -p argument\n";
    ok = false;
  }

  if(ret.patch_file_extension.empty())
    ret.patch_file_extension = DEFAULT_PATCH_EXTENSION;
  else if(!ret.patch_file_extension.starts_with('.'))
    ret.patch_file_extension = std::format(".{}", ret.patch_file_extension);

  std::cout << "source: [" << ret.source << "] target: [" << ret.target << "] patches: " << ret.patches_folder << " extension: [" << ret.patch_file_extension << "]\n";

  if(!ok)
    return std::nullopt;

  return ret;
}

std::string Win32ErrStr(unsigned long errCode)
{
  // https://learn.microsoft.com/en-us/windows/win32/sysinfo/getting-system-information

  char sysMsg[MAX_PATH] = {'\0'};
  char* p = sysMsg;

  ::FormatMessageA( FORMAT_MESSAGE_FROM_SYSTEM |
         FORMAT_MESSAGE_IGNORE_INSERTS,
         nullptr, errCode,
         MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
         sysMsg, MAX_PATH, nullptr );

  // Trim the end of the line and terminate it with a null
  // 9 - \t (horizontal tab)
  // [0 - 32) - All characters in this area excepting 9
  // 46 - . (dot)
  while (*p++)
  {
    if ((*p != 9 && *p < 32) || *p == 46)
    {
      *p = 0;
      break;
    }
  }

  return sysMsg;
}

std::vector<fs::path> list_files(std::string_view path, std::string_view extension = "")
{
  std::error_code ec;
  if(!fs::is_directory(path, ec))
    return {};

  std::vector<fs::path> ret;
  ret.reserve(10);
  if(extension.empty()) {
    for(const auto &entry : fs::directory_iterator(path)) {
      if(entry.is_regular_file(ec)) {
        ret.push_back(entry.path());
      }
    }
  }
  else {
    for(const auto &entry : fs::directory_iterator(path)) {
      auto file = entry.path();
      if(entry.is_regular_file(ec) && extension == file.extension().string()) {
        ret.push_back(std::move(file));
      }
    }
  }

  return ret;
}

std::optional<fs::path> find_target(const fs::path &src_file, const Args &args)
{
  fs::path ret_target(std::format("{}\\{}", args.target, src_file.filename().string()));
  std::error_code ec;
  if(!fs::exists(ret_target, ec))
    return std::nullopt;

  return ret_target;
};

std::optional<fs::path> find_patch(const fs::path &src_file, const Args &args)
{
  fs::path ret_patch(format("{}\\{}", args.patches_folder, src_file.filename().string()));
  ret_patch.replace_extension(args.patch_file_extension);

  std::error_code ec;
  if(!fs::exists(ret_patch, ec))
    return std::nullopt;

  return ret_patch;
};

void run_create(const fs::path &src_file, const Args &args)
{
  const auto patch_file_path = std::format("{}\\{}{}", args.patches_folder, src_file.filename().stem().string(), args.patch_file_extension);

  std::error_code ec;
  if(fs::exists(patch_file_path, ec)) {
    std::cout << "\tPatch file [" << patch_file_path << "] already exists - skipping\n";
    return;
  }

  const auto target = find_target(src_file, args);
  if(!target) {
    std::cout << "\tNo mathing target for " << src_file.filename() << " - skipping.\n";
    return;
  }

  std::cout << "\tRunning delta for " << src_file.filename() << " saving to [" << patch_file_path << "]\n";

  const BOOL ok = CreateDeltaA(
    DELTA_FILE_TYPE_SET_RAW_ONLY,
    DELTA_FLAG_IGNORE_FILE_SIZE_LIMIT,
    DELTA_FLAG_NONE,
    src_file.string().c_str(),
    target->string().c_str(),
    nullptr,
    nullptr,
    DELTA_INPUT{0},
    nullptr,
    CALG_MD5,
    patch_file_path.c_str()
  );

  if(!ok) {
    const auto error_code = ::GetLastError();
    std::cout << "\t  Failed with error code [" <<  error_code << " - " << Win32ErrStr(error_code) << "]\n";
  }
}

int run_create(const Args &args)
{
  std::error_code ec;
  if(!fs::is_directory(args.patches_folder, ec)) {
    std::cout << "Patches folder does not exist as a directory [" << args.patches_folder << "]\n";
    return ERROR_PATH_NOT_FOUND;
  }

  const auto source_files = list_files(args.source);
  if(source_files.empty()) {
    std::cout << "Source folder [" << args.source << "] is empty or does not exists\n";
    return ERROR_FILE_NOT_FOUND;
  }

  for(const fs::path &src : source_files) {
    run_create(src, args);
  }

  return 0;
}

void run_apply(const fs::path &src_file, const Args &args)
{
  const auto target_file_path = std::format("{}\\{}", args.target, src_file.filename().string());

  std::error_code ec;
  if(fs::exists(target_file_path, ec)) {
    std::cout << "\tTarget file [" << target_file_path << "] alread exists - skipping\n";
    return;
  }

  const auto patch_file = find_patch(src_file, args);
  if(!patch_file) {
    std::cout << "\tNo patch for " << src_file << "- skipping.\n";
    return;
  }

  std::cout << "\tRunning apply delta for " << src_file.filename() << " saving to [" << target_file_path << "]\n";

  const BOOL ok = ApplyDeltaA(
    DELTA_FLAG_IGNORE_FILE_SIZE_LIMIT,
    src_file.string().c_str(),
    patch_file->string().c_str(),
    target_file_path.c_str()
  );

  if(!ok) {
    const auto error_code = ::GetLastError();
    std::cout << "\t  Failed for source [" << src_file.filename() << "] error code [" <<  error_code << " - " << Win32ErrStr(error_code) << "]\n";
  }
}

int run_apply(const Args &args)
{
  std::error_code ec;
  if(!fs::is_directory(args.target, ec)) {
    std::cout << "Target does not exist as a directory [" << args.target << "]\n";
    return ERROR_PATH_NOT_FOUND;
  }

  const auto source_files = list_files(args.source);
  if(source_files.empty()) {
    std::cout << "Source folder [" << args.source << "] is empty or does not exists\n";
    return ERROR_FILE_NOT_FOUND;
  }

  for(const fs::path &src : source_files) {
    run_apply(src, args);
  }

  return 0;
}

} // namespace

int main(int argc, char *argv[])
{
  const auto args = process_args(argc, argv);
  if(!args) {
    usage();
    return ERROR_BAD_ARGUMENTS;
  }

  if(args->cmd == Args::Command::Create)
    return run_create(*args);

  if(args->cmd == Args::Command::Apply)
    return run_apply(*args);

  return 0;
}
