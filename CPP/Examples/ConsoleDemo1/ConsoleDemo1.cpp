
#include <cstdlib>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <conio.h>
#include "windows.h"

#include "..\Clipper2Lib\clipper.h"
#include "..\clipper.svg.h"

using namespace std;
using namespace Clipper2Lib;

const int display_width = 800, display_height = 600;
enum class TestType { Simple, TestFile, Benchmark, MemoryLeak };

//------------------------------------------------------------------------------
// Timer
//------------------------------------------------------------------------------

struct Timer {
private:
  _LARGE_INTEGER qpf = { 0,0 }, qpc1 = { 0,0 }, qpc2 = { 0,0 };
public:
  explicit Timer() { QueryPerformanceFrequency(&qpf); }
  void Start() { QueryPerformanceCounter(&qpc1); }
  int64_t Elapsed_Millisecs()
  {
    QueryPerformanceCounter(&qpc2);
    return static_cast<int64_t>((qpc2.QuadPart - qpc1.QuadPart) * 1000 / qpf.QuadPart);
  }
};

//------------------------------------------------------------------------------
// Functions load clipping operations from text files
//------------------------------------------------------------------------------

bool GetNumericValue(const string& line, size_t &start_pos, int64_t &value)
{
  value = 0;
  size_t line_len = line.size();
  while (start_pos < line_len && line[start_pos] == ' ') ++start_pos;
  if (start_pos >= line_len) return false;
  bool is_neg = (line[start_pos] == '-');
  if (is_neg) ++start_pos;
  size_t num_start_pos = start_pos;
  while (start_pos < line_len && line[start_pos] >= '0' && line[start_pos] <= '9') {
    value = value * 10 + (int64_t)line[start_pos] - 48;
    ++start_pos;
  }
  if (start_pos == num_start_pos) return false; //no value
  if (start_pos < line_len && line[start_pos] == ',') ++start_pos;
  if (is_neg) value = -value;
  return true;
}
//------------------------------------------------------------------------------

bool GetPath(const string &line, Paths64 &paths)
{
  size_t line_pos = 0;
  Path64 p;
  int64_t x = 0, y = 0;
  while (GetNumericValue(line, line_pos, x) && GetNumericValue(line, line_pos, y))
    p.push_back(Clipper2Lib::Point64(x, y));
  if (p.empty()) return false; 
  paths.push_back(p);
  return true;
}
//------------------------------------------------------------------------------

bool GetTestNum(ifstream &source, int test_num, bool seek_from_start,
  Paths64 &subj, Paths64 &subj_open, Paths64 &clip, 
  int64_t& area, int64_t& count,
  Clipper2Lib::ClipType &ct, Clipper2Lib::FillRule &fr)
{
  string line;
  bool found = false;
  if (seek_from_start) source.seekg(0, ios_base::beg);
  while (std::getline(source, line))
  {
    size_t line_pos = line.find("CAPTION:");
    if (line_pos == string::npos) continue;
    line_pos += 8;
    size_t line_len = line.size();
    int64_t num = 0;
    if (!GetNumericValue(line, line_pos, num)) continue;
    if (num > test_num) return false;
    if (num != test_num) continue;

    found = true;
    subj.clear(); subj_open.clear(); clip.clear(); 

    size_t pos = 0;
    while (std::getline(source, line))
    {
      if (line.find("CAPTION:") != string::npos) 
      {
        if (!pos) return false; //oops, something wrong with the test file
        //we're heading into the next test, so go back a bit ...
        source.seekg(pos, ios_base::beg);
        return true;
      }
      pos = static_cast<size_t>(source.tellg());

      if (line.find("INTERSECTION") != string::npos) 
      {
        ct = Clipper2Lib::ClipType::Intersection; continue;
      }
      else if (line.find("UNION") != string::npos) 
      {
        ct = Clipper2Lib::ClipType::Union; continue;
      }
      else if (line.find("DIFFERENCE") != string::npos) 
      {
        ct = Clipper2Lib::ClipType::Difference; continue;
      }
      else if (line.find("XOR") != string::npos) 
      {
        ct = Clipper2Lib::ClipType::Xor; continue;
      }

      if (line.find("EVENODD") != string::npos) 
      {
        fr = Clipper2Lib::FillRule::EvenOdd; continue;
      }
      else if (line.find("NONZERO") != string::npos) 
      {
        fr = Clipper2Lib::FillRule::NonZero ; continue;
      }
      else if (line.find("POSITIVE") != string::npos) 
      {
        fr = Clipper2Lib::FillRule::Positive; continue;
      }
      else if (line.find("NEGATIVE") != string::npos)
      {
        fr = Clipper2Lib::FillRule::Negative; continue;
      }
      
      else if (line.find("SOL_AREA") != string::npos)
      {
        size_t  pos_in_line = 10;
        GetNumericValue(line, pos_in_line, area); continue;
      }
      else if (line.find("SOL_COUNT") != string::npos)
      {
        size_t  pos_in_line = 11;
        GetNumericValue(line, pos_in_line, count); continue;
      }

      if (line.find("SUBJECTS_OPEN") != string::npos) 
      {
        while (getline(source, line) && GetPath(line, subj_open));
      }
      else if (line.find("SUBJECTS") != string::npos) 
      {
        while (getline(source, line) && GetPath(line, subj));
      }
      if (line.find("CLIPS") != string::npos) 
      {
        while (getline(source, line) && GetPath(line, clip));
      }
    } //inner while still lines (found)
  } //outer while still lines (not found)
  return found;
}
//------------------------------------------------------------------------------

void GetPaths(stringstream &ss, Paths64 &paths)
{
  for (;;) 
  { 
    //for each path (line) ...
    Path64 p;
    for (;;) { //for each point
      int64_t x, y;
      char char_buf;
      int c = ss.peek();
      if (c == EOF) return;
      if (c < ' ') { //assume one or more newline chars
        ss.read(&char_buf, 1);
        break;
      }
      if (!(c == '-' || (c >= '0' && c <= '9'))) return;
      if (!(ss >> x)) return; //oops!
      c = ss.peek();
      if (c != ',') return;
      ss.read(&char_buf, 1); //gobble comma
      if (!(ss >> y)) return; //oops!
      p.push_back(Clipper2Lib::Point64(x, y));
      c = ss.peek();
      if (c != ' ') break;
      ss.read(&char_buf, 1); //gobble space
    }
    if (p.size() > 2) paths.push_back(p);
    p.clear();
  }
}
//------------------------------------------------------------------------------

bool LoadFromFile(const string &filename, 
  Paths64 &subj, Paths64 &clip, 
  Clipper2Lib::ClipType &ct, Clipper2Lib::FillRule &fr)
{
  subj.clear();
  clip.clear();
  ifstream file(filename);
	if (!file.is_open()) return false;
  stringstream ss;
  ss << file.rdbuf();
  file.close();

  string line;
  bool cap_found = false;
  for (;;)
  {
    if (!getline(ss, line)) return cap_found;
    if (!cap_found) 
    {
      cap_found = line.find("CAPTION: ") != std::string::npos;
      continue; //ie keep going until caption is found
    }

    if (line.find("CLIPTYPE:") != std::string::npos)
    {
      if (line.find("INTERSECTION") != std::string::npos)
        ct = Clipper2Lib::ClipType::Intersection;
      else if (line.find("UNION") != std::string::npos)
        ct = Clipper2Lib::ClipType::Union;
      else if (line.find("DIFFERENCE") != std::string::npos)
        ct = Clipper2Lib::ClipType::Difference;
      else
        ct = Clipper2Lib::ClipType::Xor;
    }
    else if (line.find("FILLRULE:") != std::string::npos)
    {
      if (line.find("EVENODD") != std::string::npos)
        fr = Clipper2Lib::FillRule::EvenOdd;
      else
        fr = Clipper2Lib::FillRule::NonZero;
    }
    else if (line.find("SUBJECTS") != std::string::npos) GetPaths(ss, subj);
    else if (line.find("CLIPS") != std::string::npos) GetPaths(ss, clip);
    else if (line.find("CAPTION:") != std::string::npos) return true;
  }
  return cap_found;
}

//------------------------------------------------------------------------------
// Functions save clipping operations to text files
//------------------------------------------------------------------------------

void PathsToOStream(Paths64& paths, std::ostream &stream)
{
  for (Paths64::iterator paths_it = paths.begin(); paths_it != paths.end(); ++paths_it)
  {
    //watch out for empty paths
    if (paths_it->begin() == paths_it->end()) continue;
    Path64::iterator path_it, path_it_last;
    for (path_it = paths_it->begin(), path_it_last = --paths_it->end(); 
      path_it != path_it_last; ++path_it)
        stream << *path_it << ", ";
    stream << *path_it_last << endl;
  }
}
//------------------------------------------------------------------------------

void SaveToFile(const string &filename, 
  Paths64 &subj, Paths64 &clip, 
  Clipper2Lib::ClipType ct, Clipper2Lib::FillRule fr)
{
  string cliptype;
  string fillrule;

  switch (ct) 
  {
  case Clipper2Lib::ClipType::None: cliptype = "NONE"; break;
  case Clipper2Lib::ClipType::Intersection: cliptype = "INTERSECTION"; break;
  case Clipper2Lib::ClipType::Union: cliptype = "UNION"; break;
  case Clipper2Lib::ClipType::Difference: cliptype = "DIFFERENCE"; break;
  case Clipper2Lib::ClipType::Xor: cliptype = "XOR"; break;
  }

  switch (fr) 
  {
  case Clipper2Lib::FillRule::EvenOdd: fillrule = "EVENODD"; break;
  case Clipper2Lib::FillRule::NonZero : fillrule = "NONZERO"; break;
  }

  std::ofstream file;
  file.open(filename);
  file << "CAPTION: " << cliptype << " " << fillrule << endl;
  file << "CLIPTYPE: " << cliptype << endl;
  file << "FILLRULE: " << fillrule << endl;
  file << "SUBJECTS" << endl;
  PathsToOStream(subj, file);
  file << "CLIPS" << endl;
  PathsToOStream(clip, file);
  file.close();
}

//------------------------------------------------------------------------------
// Miscellaneous functions ...
//------------------------------------------------------------------------------

std::string FormatMillisecs(int64_t value)
{
  std::string sValue;
  if (value >= 1000)
  {
    if (value >= 100000)
      sValue = " secs";
    else if (value >= 10000)
      sValue = "." + to_string((value % 1000)/100) + " secs";
    else
      sValue = "." + to_string((value % 1000) /10) + " secs";
    value /= 1000;
  }
  else sValue = " msecs";
  while (value >= 1000)
  {
    sValue = "," + to_string(value % 1000) + sValue;
    value /= 1000;
  }
  sValue = to_string(value) + sValue;
  return sValue;
}
//------------------------------------------------------------------------------

Path64 Ellipse(const Rect64& rec)
{
  if (rec.IsEmpty()) return Path64();
  Point64 centre = Point64((rec.right + rec.left) / 2, (rec.bottom + rec.top) / 2);
  Point64 radii = Point64(rec.Width() /2, rec.Height() /2);
  int steps = static_cast<int>(PI * sqrt((radii.x + radii.y)/2));
  double si = std::sin(2 * PI / steps);
  double co = std::cos(2 * PI / steps);
  double dx = co, dy = si;
  Path64 result;
  result.reserve(steps);
  result.push_back(Point64(centre.x + radii.x, centre.y));
  for (int i = 1; i < steps; ++i)
  {
    result.push_back(Point64(centre.x + static_cast<int64_t>(radii.x * dx), 
      centre.y + static_cast<int64_t>(radii.y * dy)));
    double x = dx * co - dy * si;
    dy = dy * co + dx * si;
    dx = x;
  }
  return result;
}
//---------------------------------------------------------------------------

inline void MakeRandomPoly(Path64& poly, int width, int height, unsigned vertCnt)
{
  poly.resize(0);
  poly.reserve(vertCnt);
  for (unsigned i = 0; i < vertCnt; ++i)
    poly.push_back(Clipper2Lib::Point64(rand() * width, rand() * height));
}
//---------------------------------------------------------------------------

inline void SaveToSVG(const string &filename, int max_width, int max_height, 
  const Paths64 &subj, const Paths64 &subj_open,
  const Paths64 &clip, const Paths64 &solution,
  const Paths64 &solution_open,
  Clipper2Lib::FillRule fill_rule, bool show_coords = false)
{
  Clipper2Lib::SvgWriter svg;
  svg.fill_rule = fill_rule;
  svg.SetCoordsStyle("Verdana", 0xFF0000AA, 9);
  //svg.AddCaption("Clipper demo ...", 0xFF000000, 14, 20, 20);

  svg.AddPaths(subj, false, 0x1200009C, 0xCCD3D3DA, 0.8, show_coords);
  svg.AddPaths(subj_open, true, 0x0, 0xFFD3D3DA, 1.0, show_coords);
  svg.AddPaths(clip, false, 0x129C0000, 0xCCFFA07A, 0.8, show_coords);
  svg.AddPaths(solution, false, 0x6080ff9C, 0xFF003300, 0.8, show_coords);
  //for (Paths::const_iterator i = solution.cbegin(); i != solution.cend(); ++i)
  //  if (Area(*i) < 0) svg.AddPath((*i), false, 0x0, 0xFFFF0000, 0.8, show_coords);
  svg.AddPaths(solution_open, true, 0x0, 0xFF000000, 1.0, show_coords);
  svg.SaveToFile(filename, max_width, max_height, 80);
}
//------------------------------------------------------------------------------

void DoSimple()
{
  Paths64 subject, subject_open, clip;
  Paths64 solution, solution_open;
  Clipper2Lib::FillRule fr_simple = Clipper2Lib::FillRule::NonZero;
  bool show_solution_coords = false;

  subject.push_back(MakePath("500, 250, 50, 395, 325, 10, 325, 490, 50, 105"));
  clip.push_back(Ellipse(Rect64(100, 100, 400, 400)));
  //SaveToFile("simple.txt", subject, clip, ct_simple, fr_simple);
  solution = Intersect(subject, clip, fr_simple);
  solution = InflatePaths(solution, 15, JoinType::Round, EndType::Polygon);
  SaveToSVG("solution.svg", display_width, display_height,
    subject, subject_open, clip, solution, solution_open, 
    fr_simple, show_solution_coords);
  system("solution.svg");
}
//------------------------------------------------------------------------------

void DoTestsFromFile(const string& filename, 
  const int start_num, const int end_num, bool svg_draw)
{
  ifstream ifs(filename);
  if (!ifs) return;

  svg_draw = svg_draw && (end_num - start_num <= 50);

  cout << endl << "Running stored tests (from " << start_num <<
    " to " << end_num << ")" << endl << endl;

  for (int i = start_num; i <= end_num; ++i)
  {
    Paths64 subject, subject_open, clip;
    Paths64 solution, solution_open;
    Clipper2Lib::ClipType ct;
    Clipper2Lib::FillRule fr;
    int64_t area, count;

    if (GetTestNum(ifs, i, false, subject, subject_open, clip, 
      area, count, ct, fr)) 
    {
      Clipper2Lib::Clipper64 c;
      c.AddSubject(subject);
      c.AddOpenSubject(subject_open);
      c.AddClip(clip);
      c.Execute(ct, fr, solution, solution_open);
      int64_t area2 = static_cast<int64_t>(Area(solution));
      int64_t count2 = solution.size();
      int64_t count_diff = abs(count2 - count);
      if (count && count_diff > 2 && count_diff/ static_cast<double>(count) > 0.02)
        cout << "Test " << i << " counts differ: Saved val= " <<
        count << "; New val=" << count2 << endl;
      int64_t area_diff = std::abs(area2 - area);
      if (area && (area_diff > 2) && (area_diff/static_cast<double>(area)) > 0.02)
        cout << "Test " << i << " areas differ: Saved val= " <<
        area << "; New val=" << area2 << endl;
      if (svg_draw)
      {
        string filename2 = "test_" + to_string(i) + ".svg";
        SaveToSVG(filename2, display_width, display_height,
          subject, subject_open, clip, solution, solution_open, fr, false);
        system(filename2.c_str());
      }
    }
    else break;
  }
  cout << endl << "Fnished" << endl << endl;
}
//------------------------------------------------------------------------------

void DoBenchmark(int edge_cnt_start, int edge_cnt_end, int increment = 1000)
{
  Clipper2Lib::ClipType ct_benchmark = Clipper2Lib::ClipType::Intersection;//Union;//
  Clipper2Lib::FillRule fr_benchmark = Clipper2Lib::FillRule::NonZero;//EvenOdd;//

  Paths64 subject, clip;
  Paths64 solution;
  subject.resize(1);
  clip.resize(1);

  cout << "\nStarting Clipper2 Benchmarks:  " << endl << endl;
  for (int i = edge_cnt_start; i <= edge_cnt_end; i += increment)
  {
    MakeRandomPoly(subject[0], display_width, display_height, i);
    MakeRandomPoly(clip[0], display_width, display_height, i);
    //SaveToFile("benchmark_test.txt", subject, clip, ct_benchmark, fr_benchmark);

    cout << "Edges: " << i << endl;
    Timer t;
    t.Start();
    Clipper2Lib::Clipper64 clipper_benchmark;
    clipper_benchmark.AddSubject(subject);
    clipper_benchmark.AddClip(clip);
    clipper_benchmark.Execute(ct_benchmark, fr_benchmark, solution);
    int64_t msecs = t.Elapsed_Millisecs();
    cout << FormatMillisecs(msecs) << endl << endl;
  }
}
//------------------------------------------------------------------------------

void DoMemoryLeakTest()
{
  int edge_cnt = 1000;
  Clipper2Lib::ClipType ct_mem_leak = Clipper2Lib::ClipType::Intersection;//Union;//
  Clipper2Lib::FillRule fr_mem_leak = Clipper2Lib::FillRule::NonZero;//EvenOdd;//

  Paths64 subject, clip;
  Paths64 solution;
  subject.resize(1);
  clip.resize(1);

  MakeRandomPoly(subject[0], display_width, display_height, edge_cnt);
  MakeRandomPoly(clip[0], display_width, display_height, edge_cnt);

  _CrtMemState sOld, sNew, sDiff;
  _CrtMemCheckpoint(&sOld); //take a snapshot

  Clipper2Lib::Clipper64* clipper2 = new Clipper2Lib::Clipper64();
  clipper2->AddSubject(subject);
  clipper2->AddClip(clip);
  clipper2->Execute(ct_mem_leak, fr_mem_leak, solution);
  delete clipper2;

  solution.resize(0);
  solution.shrink_to_fit();
  _CrtMemCheckpoint(&sNew); //take another snapshot 
  if (_CrtMemDifference(&sDiff, &sOld, &sNew)) // is there is a difference
  {
    cout << "Memory leaks detected! See debugger output." << endl;
    OutputDebugString(L"-----------_CrtMemDumpStatistics ---------\r\n");
    _CrtMemDumpStatistics(&sDiff);
  }
  else
  {
    cout << "No memory leaks detected!" << endl;
  }
}

//------------------------------------------------------------------------------
// Main entry point ...
//------------------------------------------------------------------------------

int main(int argc, char* argv[])
{
  //////////////////////////////////////////////////////////////////////////
  TestType test_type = TestType::Simple;//Benchmark;//TestFile;//MemoryLeak;//
  //////////////////////////////////////////////////////////////////////////

  srand((unsigned)time(0));

  switch (test_type)
  {
  case TestType::Simple:
    DoSimple();
    break;
  case TestType::TestFile:
    DoTestsFromFile("../../Tests/tests.txt", 1, 200, false);
    break;
  case TestType::MemoryLeak:
    DoMemoryLeakTest();
    break;
  case TestType::Benchmark: 
    DoBenchmark(1000, 7000);
    break;
  }

  cout << "Press any key to continue" << endl;
  const char c = _getch();
  return 0;
}
//---------------------------------------------------------------------------
