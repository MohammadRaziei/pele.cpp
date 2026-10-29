#include "shapes.h"

#include <algorithm>
#include <cmath>
#include <functional>

#include "svg.h"
#include "theme.h"

namespace pele {
namespace {

using svg::num;
using sizer = std::function<shape_size(double, double)>;
using drawer = std::function<std::string(double, double, const std::string&, const std::string&)>;
using inset_fn = std::function<double(double, double, int, double)>;
using span_fn = std::function<double(double, double, bool)>;

constexpr double kCorner = 12;  // kept clear at each end of a straight side, for the corner


// A shape given by functions, one for each thing a shape does.
class table_shape final : public shape {
 public:
  table_shape(sizer s, drawer d, inset_fn i = nullptr, span_fn sp = nullptr, bool no_label = false)
      : size_(std::move(s)), draw_(std::move(d)), inset_(std::move(i)), span_(std::move(sp)), no_label_(no_label) {}

  shape_size size(double tw, double th) const override { return size_(tw, th); }
  std::string draw(double w, double h, const std::string& a, const std::string& line) const override { return draw_(w, h, a, line); }
  double inset(double w, double h, int side, double t) const override { return inset_ ? inset_(w, h, side, t) : 0; }
  double span(double w, double h, bool sideways) const override { return span_ ? span_(w, h, sideways) : 0; }
  bool has_label() const override { return !no_label_; }

 private:
  sizer size_;
  drawer draw_;
  inset_fn inset_;
  span_fn span_;
  bool no_label_;
};

// How far an ellipse's outline has fallen away from its bounding box, `t` from the middle of a side `half` long.
double bulge(double t, double half, double depth) {
  const double k = std::min(1.0, std::abs(t) / half);
  return depth * (1 - std::sqrt(1 - k * k));
}

const span_fn flat = [](double w, double h, bool sideways) { return (sideways ? h : w) - 2 * kCorner; };

// Straight along the top and bottom except for `cut(h)` at each end, and not straight at the sides.
span_fn flat_across(std::function<double(double)> cut) {
  return [cut](double w, double h, bool sideways) { return sideways ? 0 : w - 2 * cut(h) - 2 * kCorner; };
}

const inset_fn round_inset = [](double w, double h, int side, double t) { return (side & 1) ? bulge(t, h / 2, w / 2) : bulge(t, w / 2, h / 2); };
const span_fn round_span = [](double w, double h, bool sideways) { return (sideways ? h : w) * 0.6; };
const inset_fn cap_inset = [](double w, double, int side, double t) { return (side & 1) ? 0 : bulge(t, w / 2, std::min(12.0, std::max(6.0, w / 14))); };
const inset_fn lean_inset = [](double, double h, int side, double) { return (side & 1) ? h / 8 : 0; };

sizer fixed(double w, double h) {
  return [w, h](double, double) { return shape_size{w, h, 0}; };
}

shape_size tri_size(double tw, double th, bool flip) {
  const double w = std::max(1.6 * tw + 40, 80.0);
  const double h = (th + 10) / (1 - (tw + 8) / w);
  const double dy = h / 2 - 10 - th / 2;
  return {w, h, flip ? -dy : dy};
}

std::string path_el(const std::string& d, const std::string& a) { return "<path d=\"" + d + "\"" + a + "/>"; }

const sizer box = [](double tw, double th) { return shape_size{std::max(tw + 2 * kPadX, 2 * kPadX + 12), th + 2 * kPadY, 0}; };

std::string poly(const std::vector<double>& points, const std::string& a) {
  std::string d;
  for (std::size_t i = 0; i < points.size(); i += 2) d += (i ? " " : "") + num(points[i]) + "," + num(points[i + 1]);
  return "<polygon points=\"" + d + "\"" + a + "/>";
}

std::string rect(double w, double h, const std::string& a, const std::string& rx = "") {
  return "<rect x=\"" + num(-w / 2) + "\" y=\"" + num(-h / 2) + "\" width=\"" + num(w) + "\" height=\"" + num(h) + "\"" + rx + a + "/>";
}

std::string brace(double x, double h, double dir, const std::string& line) {
  const double y = h / 2;
  const double k = 6 * dir;
  return "<path d=\"M" + num(x) + "," + num(-y) + "q" + num(k) + ",0 " + num(k) + ",6V-6q0,6 " + num(k) + ",6q" + num(-k) + ",0 " + num(-k) + ",6V" + num(y - 6) +
         "q0,6 " + num(-k) + ",6\" fill=\"none\"" + line + "/>";
}

std::string wave(double w, double h, const std::string& a) {
  const double x = w / 2;
  const double top = -h / 2;
  const double base = h / 2 - 10;
  const double q = w / 4;
  return "<path d=\"M" + num(-x) + "," + num(top) + "H" + num(x) + "V" + num(base) + "q" + num(-q) + ",-12 " + num(-2 * q) + ",0t" + num(-2 * q) + ",0Z\"" + a + "/>";
}

std::string integer_text(int v) { return std::to_string(v); }

}  // namespace

shape_registry::shape_registry() {
  auto add = [this](const char* name, sizer s, drawer d, inset_fn i = nullptr, span_fn sp = nullptr, bool no_label = false) {
    shapes_[name] = std::make_unique<table_shape>(std::move(s), std::move(d), std::move(i), std::move(sp), no_label);
  };
  const double PX = kPadX;
  const double PY = kPadY;

  add("rect", box, [](double w, double h, const std::string& a, const std::string&) { return rect(w, h, theme::rounded(a)); }, nullptr, flat);
  add("rounded", box, [](double w, double h, const std::string& a, const std::string&) { return rect(w, h, a, " rx=\"" + num(std::min(12.0, h / 2)) + "\""); }, nullptr, flat);
  add("stadium",
      [=](double tw, double th) {
        const double h = th + 2 * PY;
        return shape_size{tw + h * 0.5 + 2 * PX - 8, h, 0};
      },
      [](double w, double h, const std::string& a, const std::string&) { return rect(w, h, a, " rx=\"" + num(h / 2) + "\""); },
      [](double, double h, int side, double t) { return (side & 1) ? bulge(t, h / 2, h / 2) : 0; },
      [](double w, double h, bool sideways) { return sideways ? h * 0.6 : w - h; });
  add("fr-rect", [=](double tw, double th) { return shape_size{tw + 2 * PX + 16, th + 2 * PY, 0}; },
      [](double w, double h, const std::string& a, const std::string& line) {
        const double x = w / 2 - 8;
        return rect(w, h, theme::rounded(a)) + "<path d=\"M" + num(-x) + "," + num(-h / 2) + "V" + num(h / 2) + "M" + num(x) + "," + num(-h / 2) + "V" + num(h / 2) + "\" fill=\"none\"" + line + "/>";
      },
      nullptr, flat);
  add("cyl",
      [=](double tw, double th) {
        const double w = std::max(tw + 2 * PX, 56.0);
        const double ry = std::min(12.0, std::max(6.0, w / 14));
        return shape_size{w, th + 2 * PY + 2 * ry, ry / 2};
      },
      [](double w, double h, const std::string& a, const std::string& line) {
        const double rx = w / 2;
        const double ry = std::min(12.0, std::max(6.0, w / 14));
        const double top = -h / 2 + ry;
        const double bottom = h / 2 - ry;
        return "<path d=\"M" + num(-rx) + "," + num(top) + "A" + num(rx) + "," + num(ry) + " 0 0 1 " + num(rx) + "," + num(top) + "V" + num(bottom) + "A" + num(rx) + "," + num(ry) +
               " 0 0 1 " + num(-rx) + "," + num(bottom) + "Z\"" + a + "/>" + "<path d=\"M" + num(-rx) + "," + num(top) + "A" + num(rx) + "," + num(ry) + " 0 0 0 " + num(rx) + "," +
               num(top) + "\" fill=\"none\"" + line + "/>";
      },
      cap_inset, [](double w, double h, bool sideways) { return sideways ? h - 2 * kCorner - 12 : w * 0.6; });
  add("circle",
      [](double tw, double th) {
        const double d = std::max(std::sqrt(tw * tw + th * th) + 16, 44.0);
        return shape_size{d, d, 0};
      },
      [](double w, double, const std::string& a, const std::string&) { return "<circle r=\"" + num(w / 2) + "\"" + a + "/>"; }, round_inset, round_span);
  add("dbl-circ",
      [](double tw, double th) {
        const double d = std::max(std::sqrt(tw * tw + th * th) + 26, 54.0);
        return shape_size{d, d, 0};
      },
      [](double w, double, const std::string& a, const std::string& line) {
        return "<circle r=\"" + num(w / 2) + "\"" + a + "/><circle r=\"" + num(w / 2 - 5) + "\" fill=\"none\"" + line + "/>";
      },
      round_inset, round_span);
  add("ellipse", [=](double tw, double th) { return shape_size{tw * 1.3 + 2 * PX, th * 1.2 + 2 * PY, 0}; },
      [](double w, double h, const std::string& a, const std::string&) { return "<ellipse rx=\"" + num(w / 2) + "\" ry=\"" + num(h / 2) + "\"" + a + "/>"; }, round_inset, round_span);
  add("diam",
      [](double tw, double th) {
        const double a = tw + 16;
        const double b = th + 8;
        return shape_size{a + 1.6 * b, a / 1.6 + b, 0};
      },
      [](double w, double h, const std::string& a, const std::string&) { return poly({0, -h / 2, w / 2, 0, 0, h / 2, -w / 2, 0}, a); },
      [](double w, double h, int side, double t) { return (side & 1) ? (std::abs(t) * w) / h : (std::abs(t) * h) / w; },
      [](double w, double h, bool sideways) { return (sideways ? h : w) * 0.6; });
  add("hex",
      [=](double tw, double th) {
        const double h = th + 2 * PY;
        return shape_size{tw + 2 * PX + h / 2, h, 0};
      },
      [](double w, double h, const std::string& a, const std::string&) {
        const double m = h / 4;
        return poly({-w / 2 + m, -h / 2, w / 2 - m, -h / 2, w / 2, 0, w / 2 - m, h / 2, -w / 2 + m, h / 2, -w / 2, 0}, a);
      },
      [](double, double, int side, double t) { return (side & 1) ? std::abs(t) / 2 : 0; },
      [](double w, double h, bool sideways) { return sideways ? h * 0.5 : w - h / 2 - kCorner; });
  add("odd",
      [=](double tw, double th) {
        const double h = th + 2 * PY;
        return shape_size{tw + 2 * PX + h / 4, h, 0};
      },
      [](double w, double h, const std::string& a, const std::string&) {
        const double notch = h / 4;
        return poly({-w / 2, -h / 2, w / 2, -h / 2, w / 2, h / 2, -w / 2, h / 2, -w / 2 + notch, 0}, a);
      },
      [](double, double h, int side, double) { return side == 3 ? h / 4 : 0; }, flat_across([](double) { return 0.0; }));
  const sizer slanted = [=](double tw, double th) {
    const double h = th + 2 * PY;
    return shape_size{tw + 2 * PX + h / 2, h, 0};
  };
  add("lean-r", slanted,
      [](double w, double h, const std::string& a, const std::string&) {
        const double s = h / 4;
        return poly({-w / 2 + s, -h / 2, w / 2, -h / 2, w / 2 - s, h / 2, -w / 2, h / 2}, a);
      },
      lean_inset, flat_across([](double h) { return h / 4; }));
  add("lean-l", slanted,
      [](double w, double h, const std::string& a, const std::string&) {
        const double s = h / 4;
        return poly({-w / 2, -h / 2, w / 2 - s, -h / 2, w / 2, h / 2, -w / 2 + s, h / 2}, a);
      },
      lean_inset, flat_across([](double h) { return h / 4; }));
  add("trap-b", slanted,
      [](double w, double h, const std::string& a, const std::string&) {
        const double s = h / 4;
        return poly({-w / 2 + s, -h / 2, w / 2 - s, -h / 2, w / 2, h / 2, -w / 2, h / 2}, a);
      },
      lean_inset, flat_across([](double h) { return h / 4; }));
  add("trap-t", slanted,
      [](double w, double h, const std::string& a, const std::string&) {
        const double s = h / 4;
        return poly({-w / 2, -h / 2, w / 2, -h / 2, w / 2 - s, h / 2, -w / 2 + s, h / 2}, a);
      },
      lean_inset, flat_across([](double h) { return h / 4; }));
  add("text", [](double tw, double th) { return shape_size{tw + 8, th + 8, 0}; },
      [](double w, double h, const std::string&, const std::string&) { return rect(w, h, " fill=\"none\" stroke=\"none\""); }, nullptr, flat);
  add("sm-circ", fixed(14, 14), [](double w, double, const std::string& a, const std::string&) { return "<circle r=\"" + num(w / 2) + "\"" + a + "/>"; }, round_inset, round_span, true);
  add("f-circ", fixed(14, 14), [](double w, double, const std::string&, const std::string& line) { return "<circle r=\"" + num(w / 2) + "\" fill=\"var(--_l)\"" + line + "/>"; },
      round_inset, round_span, true);
  add("fr-circ", fixed(18, 18),
      [](double w, double, const std::string& a, const std::string& line) {
        return "<circle r=\"" + num(w / 2) + "\"" + a + "/><circle r=\"" + num(w / 2 - 4) + "\" fill=\"var(--_l)\"" + line + "/>";
      },
      round_inset, round_span, true);
  add("fork", fixed(72, 8), [](double w, double h, const std::string&, const std::string& line) { return rect(w, h, " fill=\"var(--_l)\"" + line, " rx=\"2\""); }, nullptr,
      [](double w, double h, bool sideways) { return (sideways ? h : w) - 8; }, true);
  add("notch-rect", box,
      [](double w, double h, const std::string& a, const std::string&) {
        const double c = 10;
        return poly({-w / 2 + c, -h / 2, w / 2, -h / 2, w / 2, h / 2, -w / 2, h / 2, -w / 2, -h / 2 + c}, a);
      },
      nullptr, flat);
  add("lin-rect", [=](double tw, double th) { return shape_size{tw + 2 * PX + 8, th + 2 * PY, 0}; },
      [](double w, double h, const std::string& a, const std::string& line) {
        return rect(w, h, theme::rounded(a)) + "<path d=\"M" + num(-w / 2 + 8) + "," + num(-h / 2) + "V" + num(h / 2) + "\" fill=\"none\"" + line + "/>";
      },
      nullptr, flat);
  add("div-rect", [=](double tw, double th) { return shape_size{tw + 2 * PX, th + 2 * PY + 8, 4}; },
      [](double w, double h, const std::string& a, const std::string& line) {
        return rect(w, h, theme::rounded(a)) + "<path d=\"M" + num(-w / 2) + "," + num(-h / 2 + 8) + "H" + num(w / 2) + "\" fill=\"none\"" + line + "/>";
      },
      nullptr, flat);
  add("win-pane", [=](double tw, double th) { return shape_size{tw + 2 * PX + 8, th + 2 * PY + 8, 4}; },
      [](double w, double h, const std::string& a, const std::string& line) {
        return rect(w, h, theme::rounded(a)) + "<path d=\"M" + num(-w / 2 + 8) + "," + num(-h / 2) + "V" + num(h / 2) + "M" + num(-w / 2) + "," + num(-h / 2 + 8) + "H" + num(w / 2) +
               "\" fill=\"none\"" + line + "/>";
      },
      nullptr, flat);
  add("tri", [](double tw, double th) { return tri_size(tw, th, false); },
      [](double w, double h, const std::string& a, const std::string&) { return poly({0, -h / 2, w / 2, h / 2, -w / 2, h / 2}, a); },
      [](double w, double, int side, double) { return (side & 1) ? w / 4 : 0; });
  add("flip-tri", [](double tw, double th) { return tri_size(tw, th, true); },
      [](double w, double h, const std::string& a, const std::string&) { return poly({-w / 2, -h / 2, w / 2, -h / 2, 0, h / 2}, a); },
      [](double w, double, int side, double) { return (side & 1) ? w / 4 : 0; });
  add("sl-rect", [=](double tw, double th) { return shape_size{tw + 2 * PX, th + 2 * PY + 10, 5}; },
      [](double w, double h, const std::string& a, const std::string&) { return poly({-w / 2, -h / 2 + 10, w / 2, -h / 2, w / 2, h / 2, -w / 2, h / 2}, a); },
      [](double, double, int side, double) { return side == 0 ? 5 : 0; });
  add("notch-pent", [=](double tw, double th) { return shape_size{tw + 2 * PX, th + 2 * PY, 0}; },
      [](double w, double h, const std::string& a, const std::string&) {
        const double c = 10;
        return poly({-w / 2 + c, -h / 2, w / 2 - c, -h / 2, w / 2, -h / 2 + c, w / 2, h / 2, -w / 2, h / 2, -w / 2, -h / 2 + c}, a);
      },
      nullptr, flat);
  add("hourglass", fixed(32, 40), [](double w, double h, const std::string& a, const std::string&) { return poly({-w / 2, -h / 2, w / 2, -h / 2, -w / 2, h / 2, w / 2, h / 2}, a); },
      [](double w, double, int side, double) { return (side & 1) ? w / 2 : 0; }, nullptr, true);
  add("delay",
      [=](double tw, double th) {
        const double h = th + 2 * PY;
        return shape_size{tw + 2 * PX + h / 4, h, 0};
      },
      [](double w, double h, const std::string& a, const std::string&) {
        const double r = h / 2;
        return "<path d=\"M" + num(-w / 2) + "," + num(-r) + "H" + num(w / 2 - r) + "A" + num(r) + "," + num(r) + " 0 0 1 " + num(w / 2 - r) + "," + num(r) + "H" + num(-w / 2) + "Z\"" + a + "/>";
      });
  add("h-cyl",
      [=](double tw, double th) {
        const double h = th + 2 * PY;
        const double rx = std::min(12.0, h / 4);
        return shape_size{tw + 2 * PX + 2 * rx, h, 0};
      },
      [](double w, double h, const std::string& a, const std::string& line) {
        const double ry = h / 2;
        const double rx = std::min(12.0, h / 4);
        const double l = -w / 2 + rx;
        const double r = w / 2 - rx;
        return "<path d=\"M" + num(l) + "," + num(-ry) + "H" + num(r) + "A" + num(rx) + "," + num(ry) + " 0 0 1 " + num(r) + "," + num(ry) + "H" + num(l) + "A" + num(rx) + "," + num(ry) +
               " 0 0 1 " + num(l) + "," + num(-ry) + "Z\"" + a + "/>" + "<path d=\"M" + num(r) + "," + num(-ry) + "A" + num(rx) + "," + num(ry) + " 0 0 0 " + num(r) + "," + num(ry) +
               "\" fill=\"none\"" + line + "/>";
      });
  add("cross-circ", fixed(44, 44),
      [](double w, double, const std::string& a, const std::string& line) {
        const double r = w / 2;
        const double k = r * 0.7071067811865476;
        return "<circle r=\"" + num(r) + "\"" + a + "/>" + "<path d=\"M" + num(-k) + "," + num(-k) + "L" + num(k) + "," + num(k) + "M" + num(k) + "," + num(-k) + "L" + num(-k) + "," + num(k) +
               "\" fill=\"none\"" + line + "/>";
      },
      round_inset, round_span, true);
  add("st-rect", [=](double tw, double th) { return shape_size{tw + 2 * PX + 8, th + 2 * PY + 8, 4}; },
      [](double w, double h, const std::string& a, const std::string&) {
        const double bw = w - 8;
        const double bh = h - 8;
        std::string out;
        for (int i = 0; i < 3; i++) {
          const double x = -w / 2 + (2 - i) * 4;
          const double y = -h / 2 + i * 4;
          out += "<rect x=\"" + num(x) + "\" y=\"" + num(y) + "\" width=\"" + num(bw) + "\" height=\"" + num(bh) + "\"" + theme::rounded(a) + "/>";
        }
        return out;
      },
      nullptr, [](double w, double h, bool sideways) { return (sideways ? h : w) - 2 * kCorner - 8; });
  add("doc", [=](double tw, double th) { return shape_size{tw + 2 * PX, th + 2 * PY + 10, -5}; },
      [](double w, double h, const std::string& a, const std::string&) { return wave(w, h, a); }, [](double, double, int side, double) { return side == 2 ? 10 : 0; });
  add("flag", [=](double tw, double th) { return shape_size{tw + 2 * PX, th + 2 * PY + 16, 0}; },
      [](double w, double h, const std::string& a, const std::string&) {
        const double x = w / 2;
        const double y = h / 2 - 8;
        const double q = w / 4;
        return "<path d=\"M" + num(-x) + "," + num(-y) + "q" + num(q) + ",-10 " + num(2 * q) + ",0t" + num(2 * q) + ",0V" + num(y) + "q" + num(-q) + ",10 " + num(-2 * q) + ",0t" +
               num(-2 * q) + ",0Z\"" + a + "/>";
      },
      [](double, double, int side, double) { return (side & 1) ? 0 : 8; });
  add("datastore", box,
      [](double w, double h, std::string a, const std::string& line) {
        const std::string from = "stroke=\"var(--_b)\"";
        const std::size_t at = a.find(from);
        if (at != std::string::npos) a.replace(at, from.size(), "stroke=\"none\"");
        return rect(w, h, a) + "<path d=\"M" + num(-w / 2) + "," + num(-h / 2) + "H" + num(w / 2) + "M" + num(-w / 2) + "," + num(h / 2) + "H" + num(w / 2) + "\" fill=\"none\"" + line + "/>";
      },
      nullptr, flat);
  add("folder", [=](double tw, double th) { return shape_size{std::max(tw + 2 * PX, 64.0), th + 2 * PY + 8, 4}; },
      [](double w, double h, const std::string& a, const std::string&) {
        const double x = w / 2;
        const double y = h / 2;
        const double tab = std::min(w * 0.4, 48.0);
        return path_el("M" + num(-x) + "," + num(-y) + "H" + num(-x + tab) + "L" + num(-x + tab + 8) + "," + num(-y + 8) + "H" + num(x) + "V" + num(y) + "H" + num(-x) + "Z", a);
      });
  add("bucket", [=](double tw, double th) { return shape_size{tw + 2 * PX + 20, th + 2 * PY + 10, 3}; },
      [](double w, double h, const std::string& a, const std::string& line) {
        const double x = w / 2;
        const double y = h / 2;
        const double ry = 5;
        const double taper = h / 6;
        return path_el("M" + num(-x) + "," + num(-y + ry) + "A" + num(x) + "," + num(ry) + " 0 0 1 " + num(x) + "," + num(-y + ry) + "L" + num(x - taper) + "," + num(y - ry) + "A" +
                           num(x - taper) + "," + num(ry) + " 0 0 1 " + num(-x + taper) + "," + num(y - ry) + "Z",
                       a) +
               "<path d=\"M" + num(-x) + "," + num(-y + ry) + "A" + num(x) + "," + num(ry) + " 0 0 0 " + num(x) + "," + num(-y + ry) + "\" fill=\"none\"" + line + "/>";
      },
      [](double, double h, int side, double) { return (side & 1) ? h / 12 : 0; });
  add("console", [=](double tw, double th) { return shape_size{std::max(tw + 2 * PX, 72.0), th + 2 * PY + 12, 6}; },
      [](double w, double h, const std::string& a, const std::string& line) {
        return rect(w, h, theme::rounded(a)) + "<path d=\"M" + num(-w / 2) + "," + num(-h / 2 + 12) + "H" + num(w / 2) + "M" + num(-w / 2 + 7) + "," + num(-h / 2 + 4) + "l3,2.5l-3,2.5M" +
               num(-w / 2 + 13) + "," + num(-h / 2 + 9) + "h4\" fill=\"none\"" + line + "/>";
      },
      nullptr, flat);
  add("browser", [=](double tw, double th) { return shape_size{std::max(tw + 2 * PX, 72.0), th + 2 * PY + 12, 6}; },
      [](double w, double h, const std::string& a, const std::string& line) {
        std::string dots;
        for (int i = 0; i < 3; i++) dots += "<circle cx=\"" + num(-w / 2 + 8 + i * 7) + "\" cy=\"" + num(-h / 2 + 6) + "\" r=\"1.5\" fill=\"none\"" + line + "/>";
        return rect(w, h, theme::rounded(a)) + "<path d=\"M" + num(-w / 2) + "," + num(-h / 2 + 12) + "H" + num(w / 2) + "\" fill=\"none\"" + line + "/>" + dots;
      },
      nullptr, flat);
  add("person", [=](double tw, double th) { return shape_size{std::max(tw + 2 * PX, 56.0), th + 2 * PY + 26, 13}; },
      [](double w, double h, const std::string& a, const std::string&) {
        const double top = -h / 2;
        return "<rect x=\"" + num(-w / 2) + "\" y=\"" + num(top + 26) + "\" width=\"" + num(w) + "\" height=\"" + num(h - 26) + "\" rx=\"12\"" + a + "/>" + "<circle cy=\"" +
               num(top + 14) + "\" r=\"13\"" + a + "/>";
      });
  add("bang", [=](double tw, double th) { return shape_size{tw + 2 * PX + 28, th + 2 * PY + 24, 0}; },
      [](double w, double h, const std::string& a, const std::string&) {
        const int spikes = 14;
        std::vector<double> pts;
        const double pi = 3.141592653589793;
        for (int i = 0; i < spikes * 2; i++) {
          const double angle = (pi * i) / spikes - pi / 2;
          const double k = i % 2 == 0 ? 1 : 0.82;
          pts.push_back(std::cos(angle) * (w / 2) * k);
          pts.push_back(std::sin(angle) * (h / 2) * k);
        }
        return poly(pts, a);
      },
      [](double, double, int, double) { return 3.0; });
  add("cloud", [=](double tw, double th) { return shape_size{tw + 2 * PX + 32, th + 2 * PY + 24, 0}; },
      [](double w, double h, const std::string& a, const std::string&) {
        const double x = w / 2;
        const double y = h / 2;
        const double r = y * 0.55;
        return path_el("M" + num(-x + r) + "," + num(y) + "A" + num(r) + "," + num(r) + " 0 0 1 " + num(-x + r * 0.6) + "," + num(-y * 0.1) + "A" + num(x * 0.42) + "," + num(y * 0.7) +
                           " 0 0 1 " + num(-x * 0.1) + "," + num(-y * 0.72) + "A" + num(x * 0.4) + "," + num(y * 0.62) + " 0 0 1 " + num(x * 0.62) + "," + num(-y * 0.42) + "A" +
                           num(r * 1.05) + "," + num(r * 1.05) + " 0 0 1 " + num(x - r) + "," + num(y) + "Z",
                       a);
      },
      [](double, double, int side, double) { return (side & 1) ? 2 : 1; });
  add("brace", [=](double tw, double th) { return shape_size{tw + 2 * PX + 12, th + 2 * PY, 0}; },
      [](double w, double h, const std::string&, const std::string& line) { return rect(w, h, " fill=\"none\" stroke=\"none\"") + brace(-w / 2 + 12, h, -1, line); });
  add("brace-r", [=](double tw, double th) { return shape_size{tw + 2 * PX + 12, th + 2 * PY, 0}; },
      [](double w, double h, const std::string&, const std::string& line) { return rect(w, h, " fill=\"none\" stroke=\"none\"") + brace(w / 2 - 12, h, 1, line); });
  add("braces", [=](double tw, double th) { return shape_size{tw + 2 * PX + 24, th + 2 * PY, 0}; },
      [](double w, double h, const std::string&, const std::string& line) {
        return rect(w, h, " fill=\"none\" stroke=\"none\"") + brace(-w / 2 + 12, h, -1, line) + brace(w / 2 - 12, h, 1, line);
      });
  add("bolt", fixed(36, 56),
      [](double w, double h, const std::string& a, const std::string&) {
        return poly({w * 0.15, -h / 2, -w / 2, h * 0.08, -w * 0.05, h * 0.08, -w * 0.15, h / 2, w / 2, -h * 0.08, w * 0.05, -h * 0.08}, a);
      },
      [](double w, double, int side, double) { return (side & 1) ? w / 4 : 0; }, nullptr, true);
  add("lin-cyl",
      [=](double tw, double th) {
        const double w = std::max(tw + 2 * PX, 56.0);
        const double ry = std::min(12.0, std::max(6.0, w / 14));
        return shape_size{w, th + 2 * PY + 2 * ry + 8, ry / 2 + 4};
      },
      [](double w, double h, const std::string& a, const std::string& line) {
        const double rx = w / 2;
        const double ry = std::min(12.0, std::max(6.0, w / 14));
        const double top = -h / 2 + ry;
        const double bottom = h / 2 - ry;
        auto arc = [&](double y) { return "M" + num(-rx) + "," + num(y) + "A" + num(rx) + "," + num(ry) + " 0 0 0 " + num(rx) + "," + num(y); };
        return path_el("M" + num(-rx) + "," + num(top) + "A" + num(rx) + "," + num(ry) + " 0 0 1 " + num(rx) + "," + num(top) + "V" + num(bottom) + "A" + num(rx) + "," + num(ry) +
                           " 0 0 1 " + num(-rx) + "," + num(bottom) + "Z",
                       a) +
               "<path d=\"" + arc(top) + arc(top + 8) + "\" fill=\"none\"" + line + "/>";
      },
      cap_inset, [](double w, double h, bool sideways) { return sideways ? h - 2 * kCorner - 20 : w * 0.6; });
  add("curv-trap",
      [=](double tw, double th) {
        const double h = th + 2 * PY;
        return shape_size{tw + 2 * PX + h * 0.6, h, 0};
      },
      [](double w, double h, const std::string& a, const std::string&) {
        const double x = w / 2;
        const double y = h / 2;
        const double k = h * 0.3;
        return path_el("M" + num(-x + k) + "," + num(-y) + "H" + num(x - k) + "A" + num(k) + "," + num(y) + " 0 0 1 " + num(x - k) + "," + num(y) + "H" + num(-x + k) + "L" + num(-x) + ",0Z", a);
      });
  add("docs", [=](double tw, double th) { return shape_size{tw + 2 * PX + 8, th + 2 * PY + 18, -1}; },
      [](double w, double h, const std::string& a, const std::string&) {
        std::string out;
        for (int i = 0; i < 3; i++) out += "<g transform=\"translate(" + integer_text((1 - i) * 4) + "," + integer_text((i - 1) * 4) + ")\">" + wave(w - 8, h - 8, a) + "</g>";
        return out;
      },
      [](double, double, int side, double) { return side == 2 ? 10 : 0; });
  add("bow-rect",
      [=](double tw, double th) {
        const double h = th + 2 * PY;
        return shape_size{tw + 2 * PX + h * 0.4, h, 0};
      },
      [](double w, double h, const std::string& a, const std::string&) {
        const double x = w / 2;
        const double y = h / 2;
        const double k = h * 0.2;
        return path_el("M" + num(-x + k) + "," + num(-y) + "H" + num(x) + "A" + num(k) + "," + num(y) + " 0 0 0 " + num(x) + "," + num(y) + "H" + num(-x + k) + "A" + num(k) + "," + num(y) +
                           " 0 0 1 " + num(-x + k) + "," + num(-y) + "Z",
                       a);
      },
      [](double, double h, int side, double) { return side == 1 ? h * 0.2 : 0; });
  add("tag-rect", box,
      [](double w, double h, const std::string& a, const std::string& line) {
        return rect(w, h, theme::rounded(a)) + "<path d=\"M" + num(w / 2 - 12) + "," + num(h / 2) + "L" + num(w / 2) + "," + num(h / 2 - 12) + "\" fill=\"none\"" + line + "/>";
      },
      nullptr, flat);
  add("tag-doc", [=](double tw, double th) { return shape_size{tw + 2 * PX, th + 2 * PY + 10, -5}; },
      [](double w, double h, const std::string& a, const std::string& line) {
        return wave(w, h, a) + "<path d=\"M" + num(w / 2 - 12) + "," + num(h / 2 - 14) + "L" + num(w / 2) + "," + num(h / 2 - 26) + "\" fill=\"none\"" + line + "/>";
      },
      [](double, double, int side, double) { return side == 2 ? 10 : 0; });
  add("lin-doc", [=](double tw, double th) { return shape_size{tw + 2 * PX + 8, th + 2 * PY + 10, -5}; },
      [](double w, double h, const std::string& a, const std::string& line) {
        return wave(w, h, a) + "<path d=\"M" + num(-w / 2 + 8) + "," + num(-h / 2) + "V" + num(h / 2 - 6) + "\" fill=\"none\"" + line + "/>";
      },
      [](double, double, int side, double) { return side == 2 ? 10 : 0; });
  add("collapsedGroup", [=](double tw, double th) { return shape_size{tw + 2 * PX + 8, th + 2 * PY + 8, -2}; },
      [](double w, double h, const std::string& a, const std::string&) {
        return "<rect x=\"" + num(-w / 2 + 6) + "\" y=\"" + num(-h / 2 + 8) + "\" width=\"" + num(w - 12) + "\" height=\"" + num(h - 8) + "\"" + theme::rounded(a) + "/>" + "<rect x=\"" +
               num(-w / 2) + "\" y=\"" + num(-h / 2) + "\" width=\"" + num(w) + "\" height=\"" + num(h - 4) + "\"" + theme::rounded(a) + "/>";
      });

  fallback_ = shapes_.at("rect").get();
}

const shape_registry& shape_registry::instance() {
  static const shape_registry registry;
  return registry;
}

const shape* shape_registry::find(std::string_view name) const {
  const auto it = shapes_.find(std::string(name));
  return it == shapes_.end() ? nullptr : it->second.get();
}

const shape& shape_registry::get(std::string_view name) const {
  const shape* s = find(name);
  return s != nullptr ? *s : *fallback_;
}

double shape_registry::inset(std::string_view name, double w, double h, int side, double t) const {
  const shape* s = find(name);
  return s != nullptr ? s->inset(w, h, side, t) : 0;
}

double shape_registry::span(std::string_view name, double w, double h, bool sideways) const {
  const shape* s = find(name);
  return s != nullptr ? std::max(0.0, s->span(w, h, sideways)) : 0;
}

bool shape_registry::filled(std::string_view name) const {
  if (find(name) == nullptr) return true;
  return !(name == "text" || name == "brace" || name == "brace-r" || name == "braces");
}

bool shape_registry::has_label(std::string_view name) const {
  const shape* s = find(name);
  return s == nullptr || s->has_label();
}

int end_side(const std::vector<double>& route, std::size_t at, const compound_node& c) {
  const double dx = route[at] - c.x;
  const double dy = route[at + 1] - c.y;
  if (std::abs(std::abs(dy) - c.h / 2) < 0.5) return dy < 0 ? 0 : 2;
  if (std::abs(std::abs(dx) - c.w / 2) < 0.5) return dx < 0 ? 3 : 1;
  return -1;
}

void inset_route(std::vector<double>& route, std::size_t at, const shape_view& view, const compound_node& c) {
  if (c.is_group) return;
  const int side = end_side(route, at, c);
  const shape_registry& shapes = shape_registry::instance();
  if (side == 0 || side == 2) {
    const double amount = shapes.inset(view.shape, view.w, view.h, side, route[at] - c.x) + (c.h - view.h) / 2;
    route[at + 1] += side == 0 ? amount : -amount;
  } else if (side >= 0) {
    const double amount = shapes.inset(view.shape, view.w, view.h, side, route[at + 1] - c.y) + (c.w - view.w) / 2;
    route[at] += side == 3 ? amount : -amount;
  }
}

}  // namespace pele
