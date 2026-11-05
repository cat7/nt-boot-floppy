# SPDX-License-Identifier: GPL-2.0-only
# Copyright (C) 2026 powermac-nt-hal contributors
"""Import alias for pe-fn.py, whose hyphenated name Python cannot import directly."""
import importlib.util as _u, os as _os
_spec = _u.spec_from_file_location('pe_fn_impl', _os.path.join(_os.path.dirname(_os.path.abspath(__file__)), 'pe-fn.py'))
_m = _u.module_from_spec(_spec); _spec.loader.exec_module(_m)
load, export_descriptor, disassemble = _m.load, _m.export_descriptor, _m.disassemble
