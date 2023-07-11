using System.Collections;
using System.Collections.Generic;
using UnityEngine;
using System;
using UnityEngine.Networking;
namespace CesiumForUnity
{
    public class UnityRequestUrlOverride
    {
        public static Action<UnityWebRequest> OnWillRequest;
        public static void SetRequest(UnityWebRequest request)
        {
            if (OnWillRequest != null)
                OnWillRequest(request);
        }

    }
}