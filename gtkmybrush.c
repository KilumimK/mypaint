// gtk stock code - left gtk prefix to use the pygtk wrapper-generator easier
#include <stdio.h>
#include <string.h>
#include <glib.h>
#include <math.h>
#include "gtkmybrush.h"
#include "helpers.h"
#include "brush_dab.h" 
;  // ; needed

#define DEBUGLOG 0

#define ACTUAL_RADIUS_MIN 0.2
#define ACTUAL_RADIUS_MAX 100 //FIXME: performance problem acutally depending on CPU

#define abs(x) (((x)>0)?(x):(-(x)))

void
gtk_my_brush_set_base_value (GtkMyBrush * b, int id, float value)
{
  g_assert (id >= 0 && id < BRUSH_SETTINGS_COUNT);
  Mapping * m = b->settings[id];
  m->base_value = value;
}

void gtk_my_brush_set_mapping (GtkMyBrush * b, int id, int input, int index, float value)
{
  g_assert (id >= 0 && id < BRUSH_SETTINGS_COUNT);
  //g_print("set mapping: id=%d, input=%d, index=%d, value=%f\n", id, input, index, value);
  Mapping * m = b->settings[id];
  mapping_set (m, input, index, value);
}

void
gtk_my_brush_set_color (GtkMyBrush * b, int red, int green, int blue)
{
  g_assert (red >= 0 && red <= 255);
  g_assert (green >= 0 && green <= 255);
  g_assert (blue >= 0 && blue <= 255);
  b->color[0] = red;
  b->color[1] = green;
  b->color[2] = blue;
}

void
gtk_my_brush_set_print_inputs (GtkMyBrush * b, int value)
{
  b->print_inputs = value;
}

float
gtk_my_brush_get_painting_time (GtkMyBrush * b)
{
  return b->painting_time;
}

void
gtk_my_brush_set_painting_time (GtkMyBrush * b, float value)
{
  b->painting_time = value;
}

static void gtk_my_brush_class_init    (GtkMyBrushClass *klass);
static void gtk_my_brush_init          (GtkMyBrush      *b);
static void gtk_my_brush_finalize (GObject *object);

static gpointer parent_class;

GType
gtk_my_brush_get_type (void)
{
  static GType type = 0;

  if (!type)
    {
      static const GTypeInfo info =
      {
	sizeof (GtkMyBrushClass),
	NULL,		/* base_init */
	NULL,		/* base_finalize */
	(GClassInitFunc) gtk_my_brush_class_init,
	NULL,		/* class_finalize */
	NULL,		/* class_data */
	sizeof (GtkMyBrush),
	0,		/* n_preallocs */
	(GInstanceInitFunc) gtk_my_brush_init,
      };

      type =
	g_type_register_static (G_TYPE_OBJECT, "GtkMyBrush",
				&info, 0);
    }

  return type;
}

static void
gtk_my_brush_class_init (GtkMyBrushClass *class)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (class);
  parent_class = g_type_class_peek_parent (class);
  gobject_class->finalize = gtk_my_brush_finalize;
}

static void
gtk_my_brush_init (GtkMyBrush *b)
{
  int i;
  for (i=0; i<BRUSH_SETTINGS_COUNT; i++) {
    b->settings[i] = mapping_new(INPUT_COUNT);
  }
  b->painting_time = 0;
  // defaults will be set from python
}

static void
gtk_my_brush_finalize (GObject *object)
{
  GtkMyBrush * b;
  int i;
  g_return_if_fail (object != NULL);
  g_return_if_fail (GTK_IS_MY_BRUSH (object));
  b = GTK_MY_BRUSH (object);
  for (i=0; i<BRUSH_SETTINGS_COUNT; i++) {
    mapping_free(b->settings[i]);
  }
  G_OBJECT_CLASS (parent_class)->finalize (object);
}

GtkMyBrush*
gtk_my_brush_new (void)
{
  g_print ("This gets never called... but is needed. Strange.\n");
  return g_object_new (GTK_TYPE_MY_BRUSH, NULL);
}

// returns the fraction still left after t seconds
float exp_decay (float T_const, float t)
{
  // the argument might not make mathematical sense (whatever.)
  if (T_const <= 0.001) {
    return 0.0;
  } else {
    return exp(- t / T_const);
  }
}

void brush_reset (GtkMyBrush * b)
{
  b->time = 0; // triggers the real reset below in brush_stroke_to
}

// Update the "important" settings. (eg. actual radius, velocity)
//
// This has to be done more often than each dab, because of
// interpolation. For example if the radius is very big and suddenly
// changes to very small, then lots of time might pass until a dab
// would happen. But with the updated smaller radius, much more dabs
// should have been painted already.

void brush_update_settings_values (GtkMyBrush * b)
{
  int i;
  float pressure;
  float * settings = b->settings_value;
  float inputs[INPUT_COUNT];

  if (b->dtime < 0.0) {
    printf("Time is running backwards!\n");
    b->dtime = 0.00001;
  } else if (b->dtime == 0.0) {
    // FIXME: happens about every 10th start, workaround (against division by zero)
    b->dtime = 0.00001;
  }

  b->base_radius = expf(b->settings[BRUSH_RADIUS_LOGARITHMIC]->base_value);

  // FIXME: does happen (interpolation problem?)
  if (b->pressure < 0.0) b->pressure = 0.0;
  if (b->pressure > 1.0) b->pressure = 1.0;
  g_assert (b->pressure >= 0.0 && b->pressure <= 1.0);
  pressure = b->pressure; // could distort it here

  { // start / end stroke (for "stroke" input only)
    if (!b->stroke_started) {
      if (pressure > b->settings[BRUSH_STROKE_TRESHOLD]->base_value + 0.0001) {
        // start new stroke
        //printf("stroke start %f\n", pressure);
        b->stroke_started = 1;
        b->stroke = 0.0;
      }
    } else {
      if (pressure <= b->settings[BRUSH_STROKE_TRESHOLD]->base_value * 0.9 + 0.0001) {
        // end stroke
        //printf("stroke end\n");
        b->stroke_started = 0;
      }
    }
  }

  // now follows input handling

  float norm_dx, norm_dy, norm_dist, norm_speed;
  norm_dx = b->dx / b->dtime / b->base_radius;
  norm_dy = b->dy / b->dtime / b->base_radius;
  norm_speed = sqrt(SQR(norm_dx) + SQR(norm_dy));
  norm_dist = norm_speed * b->dtime;

  inputs[INPUT_PRESSURE] = pressure;
  inputs[INPUT_SPEED]  = b->norm_speed_slow1 * 0.002;
  inputs[INPUT_SPEED2] = b->norm_speed_slow2 * 0.005;
  inputs[INPUT_SPEED_LOG] = log(1.0 + b->norm_speed_slow1 * 0.002);
  inputs[INPUT_SPEED_SQRT] = sqrt(b->norm_speed_slow1 * 0.002);
  inputs[INPUT_RANDOM] = synced_random_double ();
  inputs[INPUT_STROKE] = MIN(b->stroke, 1.0);
  inputs[INPUT_CUSTOM] = b->custom_input;
  if (b->print_inputs) {
    g_print("press=% 4.3f, speed=% 4.4f\tspeed2=% 4.4f\tstroke=% 4.3f\tcustom=% 4.3f\n", inputs[INPUT_PRESSURE], inputs[INPUT_SPEED], inputs[INPUT_SPEED2], inputs[INPUT_STROKE], inputs[INPUT_CUSTOM]);
  }

  // OPTIMIZE:
  // Could only update those settings that can influence the dabbing process here.
  // (the ones only relevant for the actual drawing could be updated later)
  // However, this includes about half of the settings already. So never mind.
  for (i=0; i<BRUSH_SETTINGS_COUNT; i++) {
    settings[i] = mapping_calculate (b->settings[i], inputs);
  }

  {
    float fac = 1.0 - exp_decay (settings[BRUSH_SLOW_TRACKING_PER_DAB], 1.0);
    b->actual_x += (b->x - b->actual_x) * fac; // FIXME: should this depend on base radius?
    b->actual_y += (b->y - b->actual_y) * fac;
  }

  { // slow speed
    float fac;
    fac = 1.0 - exp_decay (settings[BRUSH_SPEED1_SLOWNESS], b->dtime);
    b->norm_speed_slow1 += (norm_speed - b->norm_speed_slow1) * fac;
    fac = 1.0 - exp_decay (settings[BRUSH_SPEED2_SLOWNESS], b->dtime);
    b->norm_speed_slow2 += (norm_speed - b->norm_speed_slow2) * fac;
  }
  
  { // slow speed, but as vector this time
    float fac = 1.0 - exp_decay (exp(settings[BRUSH_OFFSET_BY_SPEED_SLOWNESS]*0.01)-1.0, b->dtime);
    b->norm_dx_slow += (norm_dx - b->norm_dx_slow) * fac;
    b->norm_dy_slow += (norm_dy - b->norm_dy_slow) * fac;
  }

  { // custom input
    float fac;
    fac = 1.0 - exp_decay (settings[BRUSH_CUSTOM_INPUT_SLOWNESS], 0.1);
    b->custom_input += (settings[BRUSH_CUSTOM_INPUT] - b->custom_input) * fac;
  }

  { // stroke length
    float frequency;
    float wrap;
    frequency = expf(-settings[BRUSH_STROKE_DURATION_LOGARITHMIC]);
    b->stroke += norm_dist * frequency;
    //FIXME: why can this happen?
    if (b->stroke < 0) b->stroke = 0;
    //assert(b->stroke >= 0);
    wrap = 1.0 + settings[BRUSH_STROKE_HOLDTIME];
    if (b->stroke > wrap) {
      if (wrap > 9.9 + 1.0) {
        // "inifinity", just hold b->stroke somewhere >= 1.0
        b->stroke = 1.0;
      } else {
        //printf("fmodf(%f, %f) = ", (double)b->stroke, (double)wrap);
        b->stroke = fmodf(b->stroke, wrap);
        //printf("%f\n", (double)b->stroke);
        assert(b->stroke >= 0);
      }
    }
  }

  // change base radius (a rarely used feature)
  // FIXME: Wrong! Hack! Wrong! Use a new brush state instead!
  b->settings[BRUSH_RADIUS_LOGARITHMIC]->base_value += settings[BRUSH_CHANGE_RADIUS] * 0.01;

  // calculate final radius
  float radius_log;
  radius_log = settings[BRUSH_RADIUS_LOGARITHMIC];
  b->actual_radius = expf(radius_log);
  if (b->actual_radius < ACTUAL_RADIUS_MIN) b->actual_radius = ACTUAL_RADIUS_MIN;
  if (b->actual_radius > ACTUAL_RADIUS_MAX) b->actual_radius = ACTUAL_RADIUS_MAX;
}

// Called only from brush_stroke_to(). Calculate everything needed to
// draw the dab, then let draw_brush_dab() do the actual drawing.
//
// This is always called "directly" after brush_update_settings_values.
// The bbox parameter is a return value XXX
void brush_prepare_and_draw_dab (GtkMyBrush * b, GtkMySurfaceOld * s, Rect * bbox)
{
  float * settings = b->settings_value;
  float x, y, opaque;
  float radius;
  int i;
  gint color[3];
  int color_is_hsv;

  if (DEBUGLOG) {
    static FILE * logfile = NULL;
    if (!logfile) {
      logfile = fopen("dabinput.log", "w");
    }
    fprintf(logfile, "%f %f %f %f %f\n", b->time, b->dtime, b->x, b->dx, b->norm_dx_slow);
  }

  opaque = settings[BRUSH_OPAQUE] * settings[BRUSH_OPAQUE_MULTIPLY];
  if (opaque >= 1.0) opaque = 1.0;
  //if (opaque <= 0.0) opaque = 0.0;
  if (opaque <= 0.0) return;
  if (settings[BRUSH_OPAQUE_LINEARIZE]) {
    // OPTIMIZE: no need to recalculate this for each dab
    float alpha, beta, alpha_dab, beta_dab;
    float dabs_per_pixel;
    // dabs_per_pixel is just estimated roughly, I didn't think hard
    // about the case when the radius changes during the stroke
    dabs_per_pixel = (
      b->settings[BRUSH_DABS_PER_ACTUAL_RADIUS]->base_value + 
      b->settings[BRUSH_DABS_PER_BASIC_RADIUS]->base_value
      ) * 2.0;

    // the correction is probably not wanted if the dabs don't overlap
    if (dabs_per_pixel < 1.0) dabs_per_pixel = 1.0;

    // interpret the user-setting smoothly
    dabs_per_pixel = 1.0 + b->settings[BRUSH_OPAQUE_LINEARIZE]->base_value*(dabs_per_pixel-1.0);

    // see html/brushdab_saturation.png
    //      beta = beta_dab^dabs_per_pixel
    // <==> beta_dab = beta^(1/dabs_per_pixel)
    alpha = opaque;
    beta = 1.0-alpha;
    beta_dab = powf(beta, 1.0/dabs_per_pixel);
    alpha_dab = 1.0-beta_dab;
    opaque = alpha_dab;
  }

  x = b->actual_x;
  y = b->actual_y;

  if (settings[BRUSH_OFFSET_BY_SPEED]) {
    x += b->norm_dx_slow * settings[BRUSH_OFFSET_BY_SPEED] * 0.1 * b->base_radius;
    y += b->norm_dy_slow * settings[BRUSH_OFFSET_BY_SPEED] * 0.1 * b->base_radius;
  }

  if (settings[BRUSH_OFFSET_BY_RANDOM]) {
    x += synced_gauss_noise () * settings[BRUSH_OFFSET_BY_RANDOM] * b->base_radius;
    y += synced_gauss_noise () * settings[BRUSH_OFFSET_BY_RANDOM] * b->base_radius;
  }

  
  radius = b->actual_radius;
  if (settings[BRUSH_RADIUS_BY_RANDOM]) {
    float radius_log, alpha_correction;
    // go back to logarithmic radius to add the noise
    radius_log  = settings[BRUSH_RADIUS_LOGARITHMIC];
    radius_log += synced_gauss_noise () * settings[BRUSH_RADIUS_BY_RANDOM];
    radius = expf(radius_log);
    if (radius < ACTUAL_RADIUS_MIN) radius = ACTUAL_RADIUS_MIN;
    if (radius > ACTUAL_RADIUS_MAX) radius = ACTUAL_RADIUS_MAX;
    alpha_correction = b->actual_radius / radius;
    alpha_correction = SQR(alpha_correction);
    if (alpha_correction <= 1.0) {
      opaque *= alpha_correction;
    }
  }

  // color part
  
  for (i=0; i<3; i++) color[i] = b->color[i];
  color_is_hsv = 0;

  if (settings[BRUSH_ADAPT_COLOR_FROM_IMAGE]) {
    int px, py;
    guchar *rgb;
    float v = settings[BRUSH_ADAPT_COLOR_FROM_IMAGE];
    px = ROUND(x);
    py = ROUND(y);
    if (px < 0) px = 0;
    if (py < 0) py = 0;
    if (px > s->w-1) px = s->w - 1;
    if (py > s->h-1) py = s->h - 1;
    rgb = PixelXY(s, px, py);
    for (i=0; i<3; i++) {
      color[i] = ROUND((1.0-v)*color[i] + v*rgb[i]);
      if (color[i] < 0) color[i] = 0;
      if (color[i] > 255) color[i] = 255;
      b->color[i] = color[i];
    }
  }

  if (settings[BRUSH_COLOR_VALUE] ||
      settings[BRUSH_COLOR_SATURATION] ||
      settings[BRUSH_COLOR_HUE]) {
    color_is_hsv = 1;
    gimp_rgb_to_hsv_int (color + 0, color + 1, color + 2);
  }
  

  if (settings[BRUSH_COLOR_HUE]) {
    g_assert (color_is_hsv);
    color[0] += ROUND (settings[BRUSH_COLOR_HUE] * 64.0);
  }
  if (settings[BRUSH_COLOR_SATURATION]) {
    g_assert (color_is_hsv);
    color[1] += ROUND (settings[BRUSH_COLOR_SATURATION] * 128.0);
  }
  if (settings[BRUSH_COLOR_VALUE]) {
    g_assert (color_is_hsv);
    color[2] += ROUND (settings[BRUSH_COLOR_VALUE] * 128.0);
  }

  { // final calculations
    guchar c[3];

    g_assert(opaque >= 0);
    g_assert(opaque <= 1);
    
    if (color_is_hsv) {
      while (color[0] < 0) color[0] += 360;
      while (color[0] > 360) color[0] -= 360;
      if (color[1] < 0) color[1] = 0;
      if (color[1] > 255) color[1] = 255;
      if (color[2] < 0) color[2] = 0;
      if (color[2] > 255) color[2] = 255;
      gimp_hsv_to_rgb_int (color + 0, color + 1, color + 2);
    }
    for (i=0; i<3; i++) c[i] = color[i];

    float hardness = settings[BRUSH_HARDNESS];
    if (hardness > 1.0) hardness = 1.0;
    if (hardness < 0.0) hardness = 0.0;

    draw_brush_dab (s, bbox,
                    x, y, radius, opaque, hardness, c);
  }
}

// How many dabs will be drawn between the current and the next (x, y, pressure, time) position?
float brush_count_dabs_to (GtkMyBrush * b, float x, float y, float pressure, float time)
{
  float dx, dy, dt;
  float res1, res2, res3;
  float dist;

  if (b->actual_radius == 0.0) b->actual_radius = expf(b->settings[BRUSH_RADIUS_LOGARITHMIC]->base_value);
  if (b->actual_radius < ACTUAL_RADIUS_MIN) b->actual_radius = ACTUAL_RADIUS_MIN;
  if (b->actual_radius > ACTUAL_RADIUS_MAX) b->actual_radius = ACTUAL_RADIUS_MAX;

  if (b->base_radius == 0.0) b->base_radius = expf(b->settings[BRUSH_RADIUS_LOGARITHMIC]->base_value);
  if (b->base_radius < 0.5) b->base_radius = 0.5;
  if (b->base_radius > 500.0) b->base_radius = 500.0;

  dx = x - b->x;
  dy = y - b->y;
  //dp = pressure - b->pressure; // Not useful?
  dt = time - b->time;

  // OPTIMIZE
  dist = sqrtf (dx*dx + dy*dy);
  // FIXME: no need for base_value or for the range checks above IF always the interpolation
  //        function will be called before this one
  res1 = dist / b->actual_radius * b->settings[BRUSH_DABS_PER_ACTUAL_RADIUS]->base_value;
  res2 = dist / b->base_radius   * b->settings[BRUSH_DABS_PER_BASIC_RADIUS]->base_value;
  res3 = dt * b->settings[BRUSH_DABS_PER_SECOND]->base_value;
  return res1 + res2 + res3;
}

// Called from gtkmydrawwidget.c when a GTK event was received, with the new pointer position.
void brush_stroke_to (GtkMyBrush * b, GtkMySurfaceOld * s, float x, float y, float pressure, double dtime)
{
  // bounding box of the modified region
  Rect bbox;
  bbox.w = 0;

  double time = b->time + dtime;

  if (DEBUGLOG) { // logfile for debugging
    static FILE * logfile = NULL;
    if (!logfile) {
      logfile = fopen("rawinput.log", "w");
    }
    fprintf(logfile, "%f %f %f %f\n", time, x, y, pressure);
  }
  if (time <= b->time) {
    //g_print("timeskip  (time=%f, b->time=%f)\n", time, b->time);
    return;
  }

  if (b->time == 0 || time - b->time > 5) {
    // reset
    b->dist = 0;
    b->x = x;
    b->y = y;
    b->pressure = pressure;
    b->time = time;

    b->last_time = b->time;
    b->actual_x = b->x;
    b->actual_y = b->y;
    b->norm_dx_slow = 0.0;
    b->norm_dy_slow = 0.0;
    b->stroke_started = 0;
    b->stroke = 1.0; // start in a state as if the stroke was long finished
    b->custom_input = 0.0;

    b->dtime = 0.0001; // not sure if it this is needed
    return;
  }

  if (time == b->last_time) return;

  if (pressure > 0) {
    b->painting_time += time - b->last_time;
  }

  { // calculate the actual "virtual" cursor position
    float fac = 1.0 - exp_decay (b->settings[BRUSH_SLOW_TRACKING]->base_value, 100.0*(time - b->time));
    x = b->x + (x - b->x) * fac;
    y = b->y + (y - b->y) * fac;
  }
  // draw many (or zero) dabs to the next position
  b->dist += brush_count_dabs_to (b, x, y, pressure, time);
  if (b->dist > 300) {
    // this happens quite often, eg when moving the cursor back into the window
    //g_print ("Warning: NOT drawing %f dabs, resetting brush instead.\n", b->dist);
    b->time = 0; // reset
    return;
  }

  //g_print("dist = %f\n", b->dist);
  // Not going to recalculate dist each step.

  if (b->dist < 1.0 && time - b->dtime > 0.001) {
    // "move" the brush anyway, but draw no dab

    // Important to do this often, because brush_count_dabs_to depends
    // on the radius and the radius can depend on something that
    // changes much faster than only every dab.

    b->dx        = x - b->x;
    b->dy        = y - b->y;
    b->dpressure = pressure - b->pressure;
    b->dtime     = time - b->time;
      
    b->x        += b->dx;
    b->y        += b->dy;
    b->pressure += b->dpressure;
    b->time     += b->dtime;
      
    brush_update_settings_values (b);
  }

  while (b->dist >= 1.0) {
    { // linear interpolation (nonlinear variant was too slow, see SVN log)
      // Inside the loop because outside it produces numerical errors
      // resulting in b->pressure being small negative and such effects.
      float step;
      step = 1 / b->dist;
      b->dx        = step * (x - b->x);
      b->dy        = step * (y - b->y);
      b->dpressure = step * (pressure - b->pressure);
      b->dtime     = step * (time - b->time);
    }
    
    b->x        += b->dx;
    b->y        += b->dy;
    b->pressure += b->dpressure;
    b->time     += b->dtime;
    
    b->dist -= 1.0;
    
    brush_update_settings_values (b);
    brush_prepare_and_draw_dab (b, s, &bbox);
  }

  // not equal to b_time now unless b->dist == 0
  b->last_time = time;


  if (bbox.w > 0) {
    gtk_my_surface_modified ( GTK_MY_SURFACE (s), bbox.x, bbox.y, bbox.w, bbox.h);
  }
}

#define SIZE 256
typedef struct {
  int h;
  int s;
  int v;
  //signed char s;
  //signed char v;
} PrecalcData;

PrecalcData * precalcData[4];
int precalcDataIndex;

PrecalcData * precalc_data(float phase0)
{
  // Hint to the casual reader: some of the calculation here do not
  // what I originally intended. Not everything here will make sense.
  // It does not matter in the end, as long as the result looks good.

  int width, height;
  float width_inv, height_inv;
  int x, y, i;
  PrecalcData * result;

  width = SIZE;
  height = SIZE;
  result = malloc(sizeof(PrecalcData)*width*height);

  //phase0 = synced_random_double () * 2*M_PI;

  width_inv = 1.0/width;
  height_inv = 1.0/height;

  i = 0;
  for (y=0; y<height; y++) {
    for (x=0; x<width; x++) {
      float h, s, v, s_original, v_original;
      int dx, dy;
      float v_factor = 0.8;
      float s_factor = 0.8;
      float h_factor = 0.05;

#define factor2_func(x) ((x)*(x)*SIGN(x))
      float v_factor2 = 0.01;
      float s_factor2 = 0.01;


      h = 0;
      s = 0;
      v = 0;

      dx = x-width/2;
      dy = y-height/2;

      // basically, its x-axis = value, y-axis = saturation
      v = dx*v_factor + factor2_func(dx)*v_factor2;
      s = dy*s_factor + factor2_func(dy)*s_factor2;

      v_original = v; s_original = s;

      // overlay sine waves to color hue, not visible at center, ampilfying near the border
      if (1) {
        float amplitude, phase;
        float dist, dist2, borderdist;
        float dx_norm, dy_norm;
        float angle;
        dx_norm = dx*width_inv;
        dy_norm = dy*height_inv;

        dist2 = dx_norm*dx_norm + dy_norm*dy_norm;
        dist = sqrtf(dist2);
        borderdist = 0.5 - MAX(abs(dx_norm), abs(dy_norm));
        angle = atan2f(dy_norm, dx_norm);
        amplitude = 50 + dist2*dist2*dist2*100;
        phase = phase0 + 2*M_PI* (dist*0 + dx_norm*dx_norm*dy_norm*dy_norm*50) + angle*7;
        //h = sinf(phase) * amplitude;
        h = sinf(phase);
        h = (h>0)?h*h:-h*h;
        h *= amplitude;

        // calcualte angle to next 45-degree-line
        angle = abs(angle)/M_PI;
        if (angle > 0.5) angle -= 0.5;
        angle -= 0.25;
        angle = abs(angle) * 4;
        // angle is now in range 0..1
        // 0 = on a 45 degree line, 1 = on a horizontal or vertical line

        v = 0.6*v*angle + 0.4*v;
        h = h * angle * 1.5;
        s = s * angle * 1.0;

        // this part is for strong color variations at the borders
        if (borderdist < 0.3) {
          float fac;
          float h_new;
          fac = (1 - borderdist/0.3);
          // fac is 1 at the outermost pixels
          v = (1-fac)*v + fac*0;
          s = (1-fac)*s + fac*0;
          fac = fac*fac*0.6;
          h_new = (angle+phase0+M_PI/4)*360/(2*M_PI) * 8;
          while (h_new > h + 360/2) h_new -= 360;
          while (h_new < h - 360/2) h_new += 360;
          h = (1-fac)*h + fac*h_new;
          //h = (angle+M_PI/4)*360/(2*M_PI) * 4;
        }
      }

      {
        // undo that funky stuff on horizontal and vertical lines
        int min = abs(dx);
        if (abs(dy) < min) min = abs(dy);
        if (min < 30) {
          float mul;
          min -= 6;
          if (min < 0) min = 0;
          mul = min / (30.0-1.0-6.0);
          h = mul*h; //+ (1-mul)*0;

          v = mul*v + (1-mul)*v_original;
          s = mul*s + (1-mul)*s_original;
        }
      }

      h -= h*h_factor;

      result[i].h = (int)h;
      result[i].v = (int)v;
      result[i].s = (int)s;
      i++;
    }
  }
  return result;
}

GdkPixbuf* gtk_my_brush_get_colorselection_pixbuf (GtkMyBrush * b)
{
  GdkPixbuf* pixbuf;
  PrecalcData * pre;
  guchar * pixels;
  int rowstride, n_channels;
  int x, y;
  int h, s, v;
  int base_h, base_s, base_v;

  pixbuf = gdk_pixbuf_new (GDK_COLORSPACE_RGB, /*has_alpha*/0, /*bits_per_sample*/8, SIZE, SIZE);

  pre = precalcData[precalcDataIndex];
  if (!pre) {
    pre = precalcData[precalcDataIndex] = precalc_data(2*M_PI*(precalcDataIndex/4.0));
  }
  precalcDataIndex++;
  precalcDataIndex %= 4;

  n_channels = gdk_pixbuf_get_n_channels (pixbuf);
  g_assert (!gdk_pixbuf_get_has_alpha (pixbuf));
  g_assert (n_channels == 3);

  rowstride = gdk_pixbuf_get_rowstride (pixbuf);
  pixels = gdk_pixbuf_get_pixels (pixbuf);

  base_h = b->color[0];
  base_s = b->color[1];
  base_v = b->color[2];
  gimp_rgb_to_hsv_int (&base_h, &base_s, &base_v);

  for (y=0; y<SIZE; y++) {
    for (x=0; x<SIZE; x++) {
      guchar * p;

      h = base_h + pre->h;
      s = base_s + pre->s;
      v = base_v + pre->v;
      pre++;


      if (s < 0) { if (s < -50) { s = - (s + 50); } else { s = 0; } } 
      if (s > 255) { if (s > 255 + 50) { s = 255 - ((s-50)-255); } else { s = 255; } }
      if (v < 0) { if (v < -50) { v = - (v + 50); } else { v = 0; } }
      if (v > 255) { if (v > 255 + 50) { v = 255 - ((v-50)-255); } else { v = 255; } }

      s = s & 255;
      v = v & 255;
      h = h%360; if (h<0) h += 360;

      p = pixels + y * rowstride + x * n_channels;
      gimp_hsv_to_rgb_int (&h, &s, &v);
      p[0] = h; p[1] = s; p[2] = v;
    }
  }
  return pixbuf;
}

double gtk_my_brush_random_double (GtkMyBrush * b)
{
  return synced_random_double ();
}

void gtk_my_brush_srandom (GtkMyBrush * b, int value)
{
  synced_srandom (value);
}
