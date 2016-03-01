# ue4-steamcontroller-update

This is a temporary replacement for Epic's `SteamController` plugin as the version shipped with 4.11 and older is woefully out of date. Attempting to update the Steam SDK will cause the `SteamController` plugin to fail compilation.

This plugin does not have full feature-parity with Valve's recommended implementation, but its currently closer than what is provided and it compiles with newer Steamworks SDK versions.

# Requirements

+ You must be building UE4 from source to use this update.
+ Steamworks SDK v1.35a or later
  - [Here is a tutorial on how to update UE4's Steamworks SDK](http://allarsblog.com/2016/02/29/UpdatingSteamSDK/)

# Installation

1. Delete the entire contents of `Engine\Plugins\Runtime\Steam\SteamController`.
1. Download a copy of this repo and copy this repo's `Engine\Plugins\Runtime\Steam\SteamController` folder into your engine
1. Regenerate your Engine project files
1. Compile as usual

# Features

This rewrite currently allows you to use the names of your Action Mappings in your project as actions within your Steam Controller configuration file, allowing players to bind their Steam Controllers to game actions rather than emulating other input devices.

It does so by behind the scenes magic which redirects Steam Controller action bindings to the first bound key of an action within your project. This means that all of your in-game input events must have a key assigned to it, whether its a keyboard key or gamepad button. Your users will no longer have to be aware of what buttons your game is looking for however, and will only ever see the gameplay action name.

It currently *does not support* Axis Mappings.

This rewrite could be 'proper' of Epic would allow the firing of Input Events by their names instead of FKeys alone. If this is done, no 'behind the scenes' magic would be needed and it would be trivial to directly map Steam Controller actions to your projects actions, both Action Mappings and Axis Mappings.

# Usage

You will need an App ID to properly set up Steam Controller settings. Once you do, follow along with the Steam Controller API documentation regarding the controller script files. You can skip the parts about game implementation. When you define your controller actions, use the same exact names for your actions as they are named in your project's Action Mapping list.