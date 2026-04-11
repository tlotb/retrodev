### TO-DO list

* Language API definitions must be injected from the document and not by the language to allow differences regarding the tool or language variant we are using
* Add a "Find all references" (its prepared but not done)
* Add support for more systems (its prepared but only Amstrad is defined)
* Add more examples
* Add more common reusable code and macros for the SDK folder
* Add support for other building tools (like SDCC but not integrated, this app is NON GPL)
* Add support for Amstrad extended modes conversions
  * EGX1
  * EGX2
  * R (Rhino)
  * B0
  * B1
* Improve log , errors and warnings
* Improve help tooltips on the application
* Improve Codelens timings on the editor
  * When inside a function you use a macro, macro time should compute in function time
  * When inside a macro you use another macro or function, time should compute in macro time
* Improve usability
  * Palette solver must warn or do not allow to use an item in more than one participant declaration
    * This is something that may be true (graphics participating two zones but is unsolved)
* Optimizations
  * Palette solver can be optimized
